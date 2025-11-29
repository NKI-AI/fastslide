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

#include "fastslide/readers/aperio/aperio.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

#include "fastslide/readers/aperio/aperio_plan_builder.h"
#include "fastslide/readers/aperio/aperio_tile_executor.h"
#include "fastslide/readers/aperio/metadata_parser.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/hash.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_parser.h"

namespace fs = std::filesystem;

namespace fastslide {

// AperioReader implementation
aifocore::Result<std::unique_ptr<AperioReader>> AperioReader::Create(
    fs::path filename) {
  return CreateImpl(filename);
}

AperioReader::AperioReader(fs::path filename) : TiffBasedReader(filename) {
  // Initialize SimpleTiff index during construction
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd = -1;
  if (!simpletiff::OpenTiff(filename.string(), *tiff_index_, fd)) {
    std::cerr << "Failed to open and parse TIFF file: " << filename;
    // Note: Error will be caught during ProcessMetadata() if index is invalid
  }
}

int AperioReader::GetLevelCount() const {
  return static_cast<int>(pyramid_levels_.size());
}

aifocore::Result<LevelInfo> AperioReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= static_cast<int>(pyramid_levels_.size())) {
    return aifocore::Status(aifocore::StatusCode::kNotFound,
                            aifocore::fmt::format("Level {} not found", level));
  }

  const auto& aperio_level = pyramid_levels_[level];
  LevelInfo level_info;
  level_info.dimensions = {aperio_level.size[0], aperio_level.size[1]};
  level_info.downsample_factor = aperio_level.downsample_factor;

  return level_info;
}

const SlideProperties& AperioReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> AperioReader::GetChannelMetadata() const {
  // Aperio files typically have RGB channels
  std::vector<ChannelMetadata> metadata;
  metadata.emplace_back("RGB", "Histological stain", ColorRGB{255, 255, 255});
  return metadata;
}

std::vector<std::string> AperioReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto& img : associated_images_) {
    names.push_back(img.name);
  }
  return names;
}

aifocore::Result<ImageDimensions> AperioReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      return ImageDimensions{img.size[0], img.size[1]};
    }
  }
  return aifocore::Status(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image '{}' not found", name));
}

aifocore::Result<RGBImage> AperioReader::ReadAssociatedImage(
    std::string_view name) const {
  const AperioAssociatedInfo* info = nullptr;
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      info = &img;
      break;
    }
  }

  if (!info) {
    return aifocore::Status(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image '{}' not found", name));
  }

  // Use simpletiff to read the associated image page
  if (!tiff_index_ || info->page >= tiff_index_->NumPages()) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Invalid page {} for associated image '{}'",
                              info->page, name));
  }

  const auto& page_header = tiff_index_->Page(info->page);
  const uint32_t width = info->size[0];
  const uint32_t height = info->size[1];
  const uint16_t samples_per_pixel = page_header.samples_per_pixel;

  // Create RGBImage with proper dimensions
  RGBImage rgb_image({width, height}, ImageFormat::kRGB, DataType::kUInt8);

  // Read the page using simpletiff directly into the image buffer
  simpletiff::DecodeContext ctx;
  simpletiff::Roi roi{0, 0, width, height};
  const int stride = static_cast<int>(width) * samples_per_pixel;
  auto result = simpletiff::ReadPage(*tiff_index_, info->page, roi, ctx,
                                     rgb_image.GetData(), stride);

  if (!result) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read associated image '{}': {}", name,
                              result.error().message()));
  }

  return rgb_image;
}

// GetBestLevelForDownsample uses the base class implementation

ImageDimensions AperioReader::GetTileSize() const {
  // Try to get tile size from level 0
  if (pyramid_levels_.empty() || !tiff_index_) {
    return ImageDimensions{256, 256};  // Default for Aperio
  }

  // Get tile dimensions from the first level using simpletiff
  const uint16_t page = pyramid_levels_[0].page;
  if (page >= tiff_index_->NumPages()) {
    return ImageDimensions{256, 256};  // Default fallback
  }

  const auto& page_header = tiff_index_->Page(page);
  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index_->Tiles(page_header.payload_id);
    return ImageDimensions{tiles.tile_w, tiles.tile_h};
  }

  return ImageDimensions{256, 256};  // Default for Aperio
}

aifocore::Result<std::string> AperioReader::GetQuickHash() const {
  // OpenSlide-compatible quickhash for TIFF-based formats:
  // 1. Hash raw compressed tile data from lowest resolution level
  // 2. Hash TIFF property strings (name + value, each with null terminators)
  //
  // This matches OpenSlide's _openslide_tifflike_init_properties_and_hash():
  // - Uses raw compressed bytes (not decoded)
  // - Hashes property name and value as null-terminated strings
  // - Properties hashed: ImageDescription, Make, Model, Software, DateTime,
  //   Artist, HostComputer, Copyright, DocumentName (in this order)
  QuickHashBuilder hasher;

  // Hash tile data from the lowest resolution level
  if (pyramid_levels_.empty() || !tiff_index_) {
    return hasher.Finalize();
  }

  const auto& lowest_res = pyramid_levels_.back();
  const uint16_t page = lowest_res.page;

  if (page >= tiff_index_->NumPages()) {
    return hasher.Finalize();
  }

  const auto& page_header = tiff_index_->Page(page);

  // Get tile/strip info
  uint32_t total_tiles = 0;
  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index_->Tiles(page_header.payload_id);
    total_tiles = tiles.tiles_x * tiles.tiles_y;
  } else if (page_header.storage == simpletiff::Storage::kStrips) {
    const auto& strips = tiff_index_->Strips(page_header.payload_id);
    // Calculate number of strips
    const uint32_t rows_per_strip =
        strips.rows_per_strip > 0 ? strips.rows_per_strip : lowest_res.size[1];
    total_tiles = (lowest_res.size[1] + rows_per_strip - 1) / rows_per_strip;
  } else {
    return hasher.Finalize();
  }

  // Hash all tiles/strips from lowest resolution using ReadRawTile
  std::vector<uint8_t> raw_tile_data;
  for (uint32_t i = 0; i < total_tiles; ++i) {
    // Read raw compressed tile data using new simpletiff API
    auto result = simpletiff::ReadRawTile(*tiff_index_, page, i, raw_tile_data);

    if (result && !raw_tile_data.empty()) {
      auto hash_status = hasher.HashData(raw_tile_data);
      if (!hash_status.ok()) {
        // Continue hashing even if one tile fails
        continue;
      }
    }
  }

  // Hash TIFF properties from directory 0 (matches OpenSlide's
  // store_and_hash_properties)
  if (tiff_index_->NumPages() > 0) {
    const auto& page0 = tiff_index_->Page(0);

    // Helper lambda to hash property name + value (with null terminators)
    auto hash_string_prop = [&](const char* prop_name,
                                const std::string& value_str) {
      // Hash property name (with null terminator)
      auto name_status = hasher.HashData(
          reinterpret_cast<const uint8_t*>(prop_name), strlen(prop_name) + 1);
      if (!name_status.ok()) {
        return;  // Skip this property if hashing fails
      }

      // Hash property value (with null terminator)
      aifocore::Status value_status;
      if (!value_str.empty()) {
        value_status =
            hasher.HashData(reinterpret_cast<const uint8_t*>(value_str.c_str()),
                            value_str.length() + 1);
      } else {
        // Hash empty string with null terminator if property doesn't exist
        value_status = hasher.HashData(reinterpret_cast<const uint8_t*>(""), 1);
      }
      // Continue even if value hashing fails (quickhash is best-effort)
      if (!value_status.ok()) {
        std::cerr << "Failed to hash TIFF property " << prop_name;
      }
    };

    // Hash TIFF properties in the same order as OpenSlide
    // SimpleTiff stores ImageDescription in PageHeader::description
    hash_string_prop("tiff.ImageDescription", page0.description);

    // Other properties (Make, Model, Software, DateTime, Artist, HostComputer,
    // Copyright, DocumentName) are not currently extracted by simpletiff,
    // so we hash empty strings for them to maintain OpenSlide compatibility
    hash_string_prop("tiff.Make", "");
    hash_string_prop("tiff.Model", "");
    hash_string_prop("tiff.Software", "");
    hash_string_prop("tiff.DateTime", "");
    hash_string_prop("tiff.Artist", "");
    hash_string_prop("tiff.HostComputer", "");
    hash_string_prop("tiff.Copyright", "");
    hash_string_prop("tiff.DocumentName", "");
  }

  return hasher.Finalize();
}

Metadata AperioReader::GetMetadata() const {
  Metadata metadata;

  // Mandatory keys
  metadata[std::string(MetadataKeys::kFormat)] = std::string("Aperio");
  metadata[std::string(MetadataKeys::kLevels)] = pyramid_levels_.size();

  // Optional keys
  metadata[std::string(MetadataKeys::kMppX)] = aperio_metadata_.mpp[0];
  metadata[std::string(MetadataKeys::kMppY)] = aperio_metadata_.mpp[1];
  metadata[std::string(MetadataKeys::kMagnification)] =
      aperio_metadata_.app_mag;
  metadata[std::string(MetadataKeys::kScannerID)] = aperio_metadata_.scanner_id;
  metadata[std::string(MetadataKeys::kScannerModel)] = std::string("Aperio");
  metadata[std::string(MetadataKeys::kChannels)] =
      static_cast<size_t>(3);  // RGB
  metadata[std::string(MetadataKeys::kAssociatedImages)] =
      associated_images_.size();

  return metadata;
}

aifocore::Status AperioReader::ProcessMetadata() {
  // Load directories and extract basic information
  AIFOCORE_RETURN_IF_ERROR(LoadDirectories());

  return aifocore::Status::OkStatus();
}

aifocore::Status AperioReader::LoadDirectories() {
  // SimpleTiff index should already be initialized in Create()
  if (!tiff_index_) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "TIFF index not initialized");
  }

  pyramid_levels_.clear();
  associated_images_.clear();

  // Helper struct to track tiled directories for pyramid level detection
  struct TiledDirectoryInfo {
    uint16_t page;
    std::array<uint32_t, 2> size;
    uint64_t area;
  };

  std::vector<TiledDirectoryInfo> tiled_directories;

  // Iterate over all pages in the TIFF index
  bool metadata_extracted = false;
  for (size_t i = 0; i < tiff_index_->NumPages(); ++i) {
    const auto& page = tiff_index_->Page(i);

    // Validate dimensions
    if (page.width == 0 || page.height == 0) {
      return aifocore::Status(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Invalid image dimensions: {}x{}", page.width,
                                page.height));
    }

    // Extract metadata from first directory
    if (!metadata_extracted && i == 0) {
      if (!page.description.empty()) {
        auto status =
            formats::aperio::AperioMetadataParser::ParseFromDescription(
                page.description, aperio_metadata_);
        if (status.ok()) {
          metadata_extracted = true;
        }
        // Continue even if metadata extraction fails for this directory
      }
    }

    // Determine if this is a tiled page (pyramid level) or strip-based
    // (associated image)
    const bool is_tiled = (page.storage == simpletiff::Storage::kTiles);

    if (is_tiled) {
      // Store tiled directory info for later processing
      TiledDirectoryInfo tiled_info;
      tiled_info.page = static_cast<uint16_t>(i);
      tiled_info.size = {page.width, page.height};
      tiled_info.area = static_cast<uint64_t>(page.width) * page.height;
      tiled_directories.push_back(tiled_info);
    } else {
      // Non-tiled → associated image
      std::string name;
      if (i == 1) {
        name = "thumbnail";
      } else {
        // Parse name from ImageDescription
        if (!page.description.empty()) {
          name =
              formats::aperio::AperioMetadataParser::ParseAssociatedImageName(
                  page.description);
        }
        if (name.empty()) {
          name = "unknown";
        }
      }
      associated_images_.push_back(
          AperioAssociatedInfo{.page = static_cast<uint16_t>(i),
                               .size = {page.width, page.height},
                               .name = name});
    }
  }

  // Sort tiled directories by area (largest first)
  std::sort(tiled_directories.begin(), tiled_directories.end(),
            [](const TiledDirectoryInfo& a, const TiledDirectoryInfo& b) {
              return a.area > b.area;
            });

  // Convert to pyramid levels with proper downsample factors
  pyramid_levels_.reserve(tiled_directories.size());

  if (!tiled_directories.empty()) {
    // First (largest) becomes level 0
    const auto& level0 = tiled_directories[0];
    pyramid_levels_.push_back(
        AperioLevelInfo{.page = level0.page,
                        .size = {level0.size[0], level0.size[1]},
                        .downsample_factor = 1.0});

    // Calculate downsample factors for remaining levels
    for (size_t i = 1; i < tiled_directories.size(); ++i) {
      const auto& level = tiled_directories[i];

      // Calculate downsample as average of width and height ratios
      double downsample = (static_cast<double>(level0.size[0]) /
                               static_cast<double>(level.size[0]) +
                           static_cast<double>(level0.size[1]) /
                               static_cast<double>(level.size[1])) /
                          2.0;

      pyramid_levels_.push_back(
          AperioLevelInfo{.page = level.page,
                          .size = {level.size[0], level.size[1]},
                          .downsample_factor = downsample});
    }
  }

  return aifocore::Status::OkStatus();
}

void AperioReader::PopulateSlideProperties() {
  properties_.mpp = aifocore::Size<double, 2>{aperio_metadata_.mpp[0],
                                              aperio_metadata_.mpp[1]};
  properties_.objective_magnification = aperio_metadata_.app_mag;
  properties_.objective_name =
      aifocore::fmt::format("{}x", aperio_metadata_.app_mag);
  properties_.scanner_model =
      aifocore::fmt::format("Aperio/{}", aperio_metadata_.scanner_id);

  // Set bounds to full slide (Aperio has complete coverage)
  auto level0_or = GetLevelInfo(0);
  if (level0_or.ok()) {
    const auto& level0 = *level0_or;
    properties_.bounds =
        SlideBounds(0, 0, level0.dimensions[0], level0.dimensions[1]);
  }
}

// ============================================================================
// Two-Stage Pipeline Implementation
// ============================================================================
//
// Aperio uses a two-stage pipeline similar to MRXS:
// 1. PrepareRequest: Analyzes the requested region and determines which TIFF
//    tiles intersect it. Creates a TilePlan with operations for each tile.
//    Handles both tiled and stripped TIFF formats. Queries actual channel
//    count from TIFF (typically RGB/3 for Aperio, but supports other formats).
// 2. ExecutePlan: Reads each TIFF tile via simpletiff, extracts the needed
//    sub-region if the tile is partially clipped, and writes to output.
// ============================================================================

aifocore::Result<core::TilePlan> AperioReader::PrepareRequest(
    const core::TileRequest& request) const {
  // Delegate to plan builder which handles all planning logic
  return AperioPlanBuilder::BuildPlan(request, *this, tiff_metadata_);
}

aifocore::Status AperioReader::ExecutePlan(const core::TilePlan& plan,
                                           runtime::TileWriter& writer) const {
  // Delegate to executor which handles tile reading with handle pool
  return AperioTileExecutor::ExecutePlan(plan, *this, writer, tiff_metadata_);
}

}  // namespace fastslide
