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

#include "fastslide/readers/aperio/aperio_plan_builder.h"

#include <tiffio.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/aperio/aperio.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/tiff/tiff_file.h"

namespace fastslide {

absl::StatusOr<core::TilePlan> AperioPlanBuilder::BuildPlan(
    const core::TileRequest& request, const AperioReader& reader,
    TiffStructureMetadata& tiff_metadata) {

  core::TilePlan plan;
  plan.request = request;

  // Validate request
  RETURN_IF_ERROR(ValidateRequest(request, reader),
                  "Request validation failed");

  // Get level info
  auto level_info_or = reader.GetLevelInfo(request.level);
  if (!level_info_or.ok()) {
    return level_info_or.status();
  }
  const auto& level_info = *level_info_or;

  // Get pyramid level metadata
  const auto& pyramid_levels = reader.GetPyramidLevels();
  const auto& aperio_level = pyramid_levels[request.level];
  const uint16_t page = aperio_level.page;

  // Query TIFF structure
  RETURN_IF_ERROR(QueryTiffStructure(reader, page, level_info, tiff_metadata),
                  "Failed to query TIFF structure");

  // Determine region bounds from request
  double x, y;
  uint32_t width, height;
  DetermineRegionBounds(request, level_info, x, y, width, height);

  // Check if region is completely outside level bounds
  if (x >= level_info.dimensions[0] || y >= level_info.dimensions[1]) {
    // Return empty plan with background fill
    plan.output =
        CreateOutputSpec(width, height, tiff_metadata.samples_per_pixel);
    plan.actual_region = {
        .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
        .size = {width, height},
        .level = request.level};
    return plan;
  }

  // Clamp width and height to level bounds
  if (x + width > level_info.dimensions[0]) {
    width = level_info.dimensions[0] - static_cast<uint32_t>(x);
  }
  if (y + height > level_info.dimensions[1]) {
    height = level_info.dimensions[1] - static_cast<uint32_t>(y);
  }

  // Create tile operations for intersecting tiles
  auto operations = CreateTileOperations(request, tiff_metadata, level_info, x,
                                         y, width, height);
  plan.operations = std::move(operations);

  // Set output specification
  plan.output =
      CreateOutputSpec(width, height, tiff_metadata.samples_per_pixel);

  // Calculate cost estimates
  plan.cost = CalculateCosts(plan.operations);

  // Set actual region
  const uint32_t region_x = static_cast<uint32_t>(x);
  const uint32_t region_y = static_cast<uint32_t>(y);
  plan.actual_region = {.top_left = {region_x, region_y},
                        .size = {width, height},
                        .level = request.level};

  return plan;
}

absl::Status AperioPlanBuilder::ValidateRequest(
    const core::TileRequest& request, const AperioReader& reader) {

  if (request.level < 0 || request.level >= reader.GetLevelCount()) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }

  return absl::OkStatus();
}

void AperioPlanBuilder::DetermineRegionBounds(const core::TileRequest& request,
                                              const LevelInfo& level_info,
                                              double& x, double& y,
                                              uint32_t& width,
                                              uint32_t& height) {

  if (request.IsRegionRequest() && request.region_bounds->IsValid()) {
    // Use fractional region bounds from request
    x = request.region_bounds->x;
    y = request.region_bounds->y;
    width = static_cast<uint32_t>(std::ceil(request.region_bounds->width));
    height = static_cast<uint32_t>(std::ceil(request.region_bounds->height));
  } else {
    // Default to full level dimensions
    x = 0.0;
    y = 0.0;
    width = level_info.dimensions[0];
    height = level_info.dimensions[1];
  }
}

absl::Status AperioPlanBuilder::QueryTiffStructure(
    const AperioReader& reader, uint16_t page, const LevelInfo& level_info,
    TiffStructureMetadata& tiff_metadata) {

  // Store page number
  tiff_metadata.page = page;

  // Get TIFF file handle to query tile structure
  auto tiff_file_result = TiffFile::Create(reader.GetHandlePool());
  if (!tiff_file_result.ok()) {
    return tiff_file_result.status();
  }
  auto tiff_file = std::move(tiff_file_result.value());

  auto status = tiff_file.SetDirectory(page);
  if (!status.ok()) {
    return status;
  }

  // Query TIFF channel information
  // Aperio is typically RGB (3 channels), but we query the actual value
  // to handle edge cases (associated images, malformed files, etc.)
  uint16_t samples_per_pixel = 3;  // Default to RGB
  auto samples_result = tiff_file.GetSamplesPerPixel();
  if (samples_result.ok()) {
    samples_per_pixel = *samples_result;
  }

  // Validate channel count to prevent bad_array_new_length
  if (samples_per_pixel == 0 || samples_per_pixel > 100) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Invalid samples_per_pixel: %d (expected 1-100)",
                        samples_per_pixel));
  }
  tiff_metadata.samples_per_pixel = samples_per_pixel;

  // Get TIFF tile/strip dimensions
  const bool is_tiled = tiff_file.IsTiled();
  tiff_metadata.is_tiled = is_tiled;

  if (is_tiled) {
    auto tile_dims_result = tiff_file.GetTileDimensions();
    if (!tile_dims_result.ok()) {
      return tile_dims_result.status();
    }
    tiff_metadata.tile_width = (*tile_dims_result)[0];
    tiff_metadata.tile_height = (*tile_dims_result)[1];
  } else {
    // For strips, tile_width = image width, tile_height = rows per strip
    tiff_metadata.tile_width = level_info.dimensions[0];
    TIFF* tif = tiff_file.GetHandle();
    uint32_t rows_per_strip = 0;
    if (!TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip) ||
        rows_per_strip == 0) {
      rows_per_strip = level_info.dimensions[1];  // Single strip
    }
    tiff_metadata.tile_height = rows_per_strip;
  }

  return absl::OkStatus();
}

std::vector<core::TileReadOp> AperioPlanBuilder::CreateTileOperations(
    const core::TileRequest& request,
    const TiffStructureMetadata& tiff_metadata, const LevelInfo& level_info,
    double x, double y, uint32_t width, uint32_t height) {

  std::vector<core::TileReadOp> operations;

  const uint32_t tile_width = tiff_metadata.tile_width;
  const uint32_t tile_height = tiff_metadata.tile_height;
  const uint16_t samples_per_pixel = tiff_metadata.samples_per_pixel;
  const bool is_tiled = tiff_metadata.is_tiled;
  const uint16_t page = tiff_metadata.page;

  // Calculate which tiles intersect the requested region
  const uint32_t region_x = static_cast<uint32_t>(x);
  const uint32_t region_y = static_cast<uint32_t>(y);

  const uint32_t first_tile_x = region_x / tile_width;
  const uint32_t first_tile_y = region_y / tile_height;
  const uint32_t last_tile_x = (region_x + width - 1) / tile_width;
  const uint32_t last_tile_y = (region_y + height - 1) / tile_height;

  // Get bits per sample to calculate bytes per pixel (for cost estimation)
  uint16_t bits_per_sample = 8;  // Default to 8-bit (typical for Aperio)
  const uint32_t bytes_per_pixel =
      ((bits_per_sample + 7) / 8) * samples_per_pixel;

  // Reserve space for operations
  const size_t estimated_ops =
      static_cast<size_t>(last_tile_x - first_tile_x + 1) *
      (last_tile_y - first_tile_y + 1);
  operations.reserve(estimated_ops);

  // Create operations for each intersecting tile
  for (uint32_t tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
    for (uint32_t tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
      // Calculate tile bounds in level coordinates
      const uint32_t tile_left = tile_x * tile_width;
      const uint32_t tile_top = tile_y * tile_height;
      const uint32_t tile_right =
          std::min(tile_left + tile_width, level_info.dimensions[0]);
      const uint32_t tile_bottom =
          std::min(tile_top + tile_height, level_info.dimensions[1]);

      // Calculate intersection with requested region
      const uint32_t inter_left = std::max(tile_left, region_x);
      const uint32_t inter_top = std::max(tile_top, region_y);
      const uint32_t inter_right = std::min(tile_right, region_x + width);
      const uint32_t inter_bottom = std::min(tile_bottom, region_y + height);

      if (inter_left >= inter_right || inter_top >= inter_bottom) {
        continue;  // No intersection
      }

      const uint32_t inter_width = inter_right - inter_left;
      const uint32_t inter_height = inter_bottom - inter_top;

      // Create tile operation
      core::TileReadOp op;
      op.level = request.level;
      op.tile_coord = {tile_x, tile_y};
      op.source_id = page;

      // For TIFF, byte_offset is the tile index (not actual file offset)
      // Calculate tile index: for tiled TIFFs, for strips it's the strip number
      if (is_tiled) {
        const uint32_t tiles_across =
            (level_info.dimensions[0] + tile_width - 1) / tile_width;
        op.byte_offset = tile_y * tiles_across + tile_x;
      } else {
        op.byte_offset = tile_y;  // Strip number
      }

      // Estimate compressed tile size (actual size unknown until read)
      op.byte_size =
          tile_width * tile_height * bytes_per_pixel;  // Uncompressed estimate

      // Transform: source is the intersection within the tile,
      // dest is relative to output region origin
      const uint32_t src_x = inter_left - tile_left;
      const uint32_t src_y = inter_top - tile_top;
      const uint32_t dest_x = inter_left - region_x;
      const uint32_t dest_y = inter_top - region_y;

      op.transform.source = {src_x, src_y, inter_width, inter_height};
      op.transform.dest = {dest_x, dest_y, inter_width, inter_height};

      operations.push_back(op);
    }
  }

  return operations;
}

core::OutputSpec AperioPlanBuilder::CreateOutputSpec(
    uint32_t width, uint32_t height, uint16_t samples_per_pixel) {

  core::OutputSpec spec;
  spec.dimensions = {width, height};
  spec.channels = samples_per_pixel;
  spec.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  spec.background = {255, 255, 255, 255};  // White background

  return spec;
}

core::TilePlan::Cost AperioPlanBuilder::CalculateCosts(
    const std::vector<core::TileReadOp>& operations) {

  core::TilePlan::Cost cost;
  cost.total_tiles = operations.size();
  cost.total_bytes_to_read = 0;
  for (const auto& op : operations) {
    cost.total_bytes_to_read += op.byte_size;
  }
  cost.tiles_to_decode = cost.total_tiles;
  cost.tiles_from_cache = 0;  // Assume no cache for now
  cost.estimated_time_ms = cost.total_bytes_to_read / 1000.0;  // Rough estimate

  return cost;
}

}  // namespace fastslide
