// Copyright 2025 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fastslide/readers/tiff_based_reader.h"

#include <tiffio.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "aifocore/concepts/numeric.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/utilities/tiff/planar_interleaver.h"
#include "fastslide/utilities/tiff/tiff_cache_service.h"
#include "fastslide/utilities/tiff/tiff_file.h"
#include "fastslide/utilities/tiff/tile_utilities.h"

namespace fs = std::filesystem;

namespace fastslide {

using aifocore::fmt::format;

// Type alias for tile coordinates
using TiffTileCoordinate = tiff::TileCoordinate;

TiffBasedReader::TiffBasedReader(fs::path filename)
    : filename_(std::move(filename)),
      tiff_cache_service_(std::make_unique<tiff::TiffCacheService>()) {}

void TiffBasedReader::SetCache(std::shared_ptr<TileCache> cache) {
  SlideReader::SetCache(cache);
  tiff_cache_service_->SetCache(cache);
}

void TiffBasedReader::SetCache(std::shared_ptr<ITileCache> cache) {
  // For TIFF-based readers, we store in the old TileCache system
  // TODO(fastslide): Update tiff_cache_service to use ITileCache directly
  // For now, ITileCache is used primarily by MRXS readers
  (void)cache;  // Suppress unused parameter warning
}

absl::Status TiffBasedReader::ValidateTiffFile(const fs::path& filename) {
  return TiffFile::ValidateFile(filename);
}

absl::Status TiffBasedReader::InitializeHandlePool() {
  auto handle_pool_result = TIFFHandlePool::Create(filename_);
  if (!handle_pool_result.ok()) {
    return handle_pool_result.status();
  }
  handle_pool_ = std::move(handle_pool_result.value());
  return absl::OkStatus();
}

std::vector<uint8_t> TiffBasedReader::ReadTiffRegion(
    uint16_t page, const RegionSpec& region, uint32_t& actual_width,
    uint32_t& actual_height, uint32_t bytes_per_pixel) const {

  // Create TiffFile wrapper using the handle pool
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return {};
  }
  auto tiff_file = std::move(tiff_file_result.value());

  return ReadTiffRegionWithFile(tiff_file, page, region, actual_width,
                                actual_height, bytes_per_pixel);
}

std::vector<uint8_t> TiffBasedReader::ReadTiffRegionWithFile(
    TiffFile& tiff_file, uint16_t page, const RegionSpec& region,
    uint32_t& actual_width, uint32_t& actual_height,
    uint32_t bytes_per_pixel) const {

  // Set directory
  auto status = tiff_file.SetDirectory(page);

  if (!status.ok()) {
    return {};
  }

  // Get image dimensions
  auto image_dims_result = tiff_file.GetImageDimensions();
  if (!image_dims_result.ok()) {
    return {};
  }
  const auto& image_dims = image_dims_result.value();

  // Clamp region to image bounds using elementwise operations
  auto remaining_space = image_dims - region.top_left;
  auto actual_size = Min(region.size, remaining_space);
  actual_width = actual_size[0];
  actual_height = actual_size[1];

  if (actual_width == 0 || actual_height == 0) {
    return {};
  }

  // Allocate vector for region
  std::vector<uint8_t> buffer(actual_width * actual_height * bytes_per_pixel);

  // Check if image is tiled
  bool is_tiled = tiff_file.IsTiled();

  absl::Status read_status;
  if (is_tiled) {
    read_status = ReadTiledRegion(tiff_file, buffer.data(), page, region,
                                  bytes_per_pixel);
  } else {
    read_status = ReadStripRegion(tiff_file, buffer.data(), page, region,
                                  bytes_per_pixel);
  }

  if (!read_status.ok()) {
    return {};
  }

  return buffer;
}

absl::Status TiffBasedReader::ReadTiledRegion(TiffFile& tiff_file,
                                              uint8_t* buffer, uint16_t page,
                                              const RegionSpec& region,
                                              uint32_t bytes_per_pixel) const {
  // Get tile dimensions
  TiffTileDimensions tile_dims;
  ASSIGN_OR_RETURN(tile_dims, tiff_file.GetTileDimensions(),
                   "Failed to get tile dimensions");

  // Get tile size
  size_t tile_size = 0;
  ASSIGN_OR_RETURN(tile_size, tiff_file.GetTileSize(),
                   "Failed to get tile size");

  // Check planar configuration
  TiffPlanarConfig planar_config;
  ASSIGN_OR_RETURN(planar_config, tiff_file.GetPlanarConfig(),
                   "Failed to get planar configuration");

  // Get number of samples per pixel
  uint16_t samples_per_pixel = 0;
  ASSIGN_OR_RETURN(samples_per_pixel, tiff_file.GetSamplesPerPixel(),
                   "Failed to get samples per pixel");

  // Prepare configuration for planar interleaving
  const uint32_t bytes_per_sample = bytes_per_pixel / samples_per_pixel;
  const tiff::PlanarInterleaver::Config interleaver_config{
      .planar_config = planar_config,
      .samples_per_pixel = samples_per_pixel,
      .bytes_per_sample = bytes_per_sample,
      .data_size = tile_size};

  std::vector<uint8_t> tile_buffer(tile_size);

  // Iterate through tiles that intersect with our region
  for (const auto& tile_coords : tiff::TileCoordinateRange(region, tile_dims)) {
    // Get tile using TIFF cache service
    auto tile_result = tiff_cache_service_->GetTileFromTiff(
        filename_.string(), page, tile_coords, tiff_file, tile_dims,
        interleaver_config, bytes_per_pixel);

    if (!tile_result.ok()) {
      continue;  // Skip failed tiles
    }

    // Copy cached data to tile buffer
    auto cached_tile = tile_result.value();
    if (cached_tile && cached_tile->data.size() <= tile_buffer.size()) {
      std::copy(cached_tile->data.begin(), cached_tile->data.end(),
                tile_buffer.begin());
    } else {
      continue;  // Skip if tile data is invalid
    }

    {
      // Copy relevant portion of tile to output buffer
      tiff::CopyTileToBuffer(tile_buffer.data(), buffer, tile_dims[0],
                             tile_dims[1], tile_coords[0], tile_coords[1],
                             region.top_left[0], region.top_left[1],
                             region.size[0], region.size[1], bytes_per_pixel);
    }
  }

  return absl::OkStatus();
}

absl::Status TiffBasedReader::ReadStripRegion(TiffFile& tiff_file,
                                              uint8_t* buffer, uint16_t page,
                                              const RegionSpec& region,
                                              uint32_t bytes_per_pixel) const {
  // Get image dimensions to determine width for scanline caching
  TiffImageDimensions image_dims;
  ASSIGN_OR_RETURN(image_dims, tiff_file.GetImageDimensions(),
                   "Failed to get image dimensions");
  uint32_t image_width = image_dims[0];

  // Get scanline size
  size_t scanline_size;
  ASSIGN_OR_RETURN(scanline_size, tiff_file.GetScanlineSize(),
                   "Failed to get scanline size");

  // Check planar configuration
  TiffPlanarConfig planar_config;
  ASSIGN_OR_RETURN(planar_config, tiff_file.GetPlanarConfig(),
                   "Failed to get planar configuration");

  // Get number of samples per pixel
  uint16_t samples_per_pixel = 0;
  ASSIGN_OR_RETURN(samples_per_pixel, tiff_file.GetSamplesPerPixel(),
                   "Failed to get samples per pixel");

  // Prepare configuration for planar interleaving
  const uint32_t bytes_per_sample = bytes_per_pixel / samples_per_pixel;
  const tiff::PlanarInterleaver::Config interleaver_config{
      .planar_config = planar_config,
      .samples_per_pixel = samples_per_pixel,
      .bytes_per_sample = bytes_per_sample,
      .data_size = scanline_size};

  std::vector<uint8_t> scanline_buffer(scanline_size);

  for (uint32_t row = 0; row < region.size[1]; ++row) {
    uint32_t image_row = region.top_left[1] + row;

    // Get scanline using TIFF cache service
    auto scanline_result = tiff_cache_service_->GetScanlineFromTiff(
        filename_.string(), static_cast<uint16_t>(page), image_row, tiff_file,
        image_width, interleaver_config, bytes_per_pixel);

    if (!scanline_result.ok()) {
      return absl::InternalError("Failed to read scanline");
    }

    // Copy cached data to scanline buffer
    auto cached_scanline = scanline_result.value();
    if (cached_scanline &&
        cached_scanline->data.size() <= scanline_buffer.size()) {
      std::copy(cached_scanline->data.begin(), cached_scanline->data.end(),
                scanline_buffer.begin());
    } else {
      return absl::InternalError("Invalid scanline data");
    }

    // Copy the relevant portion of the scanline
    std::copy(scanline_buffer.data() + region.top_left[0] * bytes_per_pixel,
              scanline_buffer.data() +
                  (region.top_left[0] + region.size[0]) * bytes_per_pixel,
              buffer + row * region.size[0] * bytes_per_pixel);
  }

  return absl::OkStatus();
}

absl::StatusOr<RGBImage> TiffBasedReader::ReadAssociatedImageFromPage(
    uint16_t page, uint32_t width, uint32_t height,
    const std::string& name) const {

  // Create TiffFile wrapper using the handle pool
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return MAKE_STATUSOR(RGBImage, absl::StatusCode::kInternal,
                         "Failed to create TiffFile wrapper");
  }
  auto tiff_file = std::move(tiff_file_result.value());

  return ReadAssociatedImageFromPageWithFile(tiff_file, page, width, height,
                                             name);
}

absl::StatusOr<RGBImage> TiffBasedReader::ReadAssociatedImageFromPageWithFile(
    TiffFile& tiff_file, uint16_t page, uint32_t width, uint32_t height,
    const std::string& name) const {

  // Set directory
  RETURN_IF_ERROR(tiff_file.SetDirectory(page),
                  format("Failed to set directory for {}", name));

  // Allocate RGBA buffer using standard C++ memory management
  std::vector<uint32_t> raster(width * height);

  // Read RGBA image, these are usually the associated images in the TIFF file
  RETURN_IF_ERROR(tiff_file.ReadRGBAImage(raster.data(), width, height, true),
                  "Failed to read RGBA data");

  ImageDimensions dims{width, height};
  RGBImage rgb_image(dims, ImageFormat::kRGB, DataType::kUInt8);

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      uint32_t flipped_y = height - 1 - y;
      uint32_t rgba = raster[flipped_y * width + x];

      rgb_image.At<uint8_t>(y, x, 0) = TIFFGetR(rgba);
      rgb_image.At<uint8_t>(y, x, 1) = TIFFGetG(rgba);
      rgb_image.At<uint8_t>(y, x, 2) = TIFFGetB(rgba);
    }
  }

  return rgb_image;
}

int TiffBasedReader::GetBestLevelForDownsampleImpl(
    double downsample, int level_count,
    std::function<double(int)> get_level_downsample) const {
  if (level_count == 0) {
    return 0;
  }

  int best_level = 0;
  double best_diff = std::abs(1.0 - downsample);

  for (int level = 0; level < level_count; ++level) {
    double level_downsample = get_level_downsample(level);
    double diff = std::abs(level_downsample - downsample);
    if (diff < best_diff) {
      best_diff = diff;
      best_level = level;
    }
  }

  return best_level;
}

}  // namespace fastslide
