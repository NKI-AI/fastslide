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

#include "fastslide/readers/qptiff/qptiff_plan_builder.h"

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"

namespace fastslide {

absl::StatusOr<core::TilePlan> QptiffPlanBuilder::BuildPlan(
    const core::TileRequest& request,
    const std::vector<QpTiffLevelInfo>& pyramid,
    PlanarConfig output_planar_config, TiffFile& tiff_file) {

  core::TilePlan plan;
  plan.request = request;

  // Validate request
  RETURN_IF_ERROR(ValidateRequest(request, pyramid),
                  "Request validation failed");

  const QpTiffLevelInfo& level_info = pyramid[request.level];
  const size_t num_channels = level_info.pages.size();

  // Set directory to first channel to query TIFF structure
  RETURN_IF_ERROR(tiff_file.SetDirectory(level_info.pages[0]),
                  "Failed to set directory");

  // Get bits per sample to determine pixel format and bytes per pixel
  uint16_t bits_per_sample = 8;  // Default to 8-bit
  auto bits_result = tiff_file.GetBitsPerSample();
  if (bits_result.ok()) {
    bits_per_sample = *bits_result;
  }
  const uint32_t bytes_per_sample = (bits_per_sample + 7) / 8;

  // Convert bits per sample to pixel format
  core::OutputSpec::PixelFormat pixel_format;
  if (bits_per_sample <= 8) {
    pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  } else if (bits_per_sample <= 16) {
    pixel_format = core::OutputSpec::PixelFormat::kUInt16;
  } else {
    pixel_format = core::OutputSpec::PixelFormat::kFloat32;
  }

  // Determine region bounds from request
  double x, y;
  uint32_t width, height;
  DetermineRegionBounds(request, level_info, x, y, width, height);

  // Clamp region to level bounds
  if (x >= level_info.size[0] || y >= level_info.size[1]) {
    // Region completely outside level bounds - return empty plan
    plan.output.dimensions = {width, height};
    plan.output.channels = static_cast<uint32_t>(num_channels);
    plan.output.pixel_format = pixel_format;
    plan.output.planar_config = output_planar_config;
    plan.output.background = {0, 0, 0, 255};  // Black background
    plan.actual_region = {
        .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
        .size = {width, height},
        .level = request.level};
    return plan;
  }

  // Clamp width and height to level bounds
  if (x + width > level_info.size[0]) {
    width = level_info.size[0] - static_cast<uint32_t>(x);
  }
  if (y + height > level_info.size[1]) {
    height = level_info.size[1] - static_cast<uint32_t>(y);
  }

  // Get tile dimensions
  uint32_t tile_width, tile_height;
  bool is_tiled;
  RETURN_IF_ERROR(GetTileDimensions(tiff_file, level_info, tile_width,
                                    tile_height, is_tiled),
                  "Failed to get tile dimensions");

  // Create tile operations
  const uint32_t region_x = static_cast<uint32_t>(x);
  const uint32_t region_y = static_cast<uint32_t>(y);

  auto operations = CreateTileOperations(
      request, level_info, region_x, region_y, width, height, tile_width,
      tile_height, is_tiled, bytes_per_sample);

  plan.operations = std::move(operations);

  // Set output specification
  plan.output.dimensions = {width, height};
  plan.output.channels = static_cast<uint32_t>(num_channels);
  plan.output.pixel_format = pixel_format;
  plan.output.planar_config = output_planar_config;
  plan.output.background = {0, 0, 0, 255};  // Black background

  // Estimate costs
  plan.cost.total_tiles = plan.operations.size();
  plan.cost.total_bytes_to_read = 0;
  for (const auto& op : plan.operations) {
    plan.cost.total_bytes_to_read += op.byte_size;
  }
  plan.cost.tiles_to_decode = plan.cost.total_tiles;

  // Set actual region
  plan.actual_region = {.top_left = {region_x, region_y},
                        .size = {width, height},
                        .level = request.level};

  return plan;
}

absl::Status QptiffPlanBuilder::ValidateRequest(
    const core::TileRequest& request,
    const std::vector<QpTiffLevelInfo>& pyramid) {

  if (request.level < 0 ||
      static_cast<size_t>(request.level) >= pyramid.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Invalid level: %d", request.level));
  }

  const QpTiffLevelInfo& level_info = pyramid[request.level];
  const size_t num_channels = level_info.pages.size();

  if (num_channels == 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Level %d has no pages/channels", request.level));
  }
  if (num_channels > 1000) {  // Reasonable upper bound for spectral imaging
    return absl::InvalidArgumentError(
        absl::StrFormat("Level %d has too many channels: %zu (max 1000)",
                        request.level, num_channels));
  }

  return absl::OkStatus();
}

void QptiffPlanBuilder::DetermineRegionBounds(const core::TileRequest& request,
                                              const QpTiffLevelInfo& level_info,
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
    width = level_info.size[0];
    height = level_info.size[1];
  }
}

absl::Status QptiffPlanBuilder::GetTileDimensions(
    TiffFile& tiff_file, const QpTiffLevelInfo& level_info,
    uint32_t& tile_width, uint32_t& tile_height, bool& is_tiled) {

  is_tiled = tiff_file.IsTiled();

  if (is_tiled) {
    auto tile_dims_result = tiff_file.GetTileDimensions();
    if (!tile_dims_result.ok()) {
      return tile_dims_result.status();
    }
    tile_width = (*tile_dims_result)[0];
    tile_height = (*tile_dims_result)[1];
  } else {
    // For strips, tile_width = image width, tile_height = rows per strip
    tile_width = level_info.size[0];
    TIFF* tif = tiff_file.GetHandle();
    uint32_t rows_per_strip = 0;
    if (!TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip) ||
        rows_per_strip == 0) {
      rows_per_strip = level_info.size[1];  // Single strip
    }
    tile_height = rows_per_strip;
  }

  return absl::OkStatus();
}

std::vector<core::TileReadOp> QptiffPlanBuilder::CreateTileOperations(
    const core::TileRequest& request, const QpTiffLevelInfo& level_info,
    uint32_t region_x, uint32_t region_y, uint32_t width, uint32_t height,
    uint32_t tile_width, uint32_t tile_height, bool is_tiled,
    uint32_t bytes_per_sample) {

  std::vector<core::TileReadOp> operations;

  // Calculate which tiles intersect the requested region
  const uint32_t first_tile_x = region_x / tile_width;
  const uint32_t first_tile_y = region_y / tile_height;
  const uint32_t last_tile_x = (region_x + width - 1) / tile_width;
  const uint32_t last_tile_y = (region_y + height - 1) / tile_height;

  const size_t num_channels = level_info.pages.size();

  // For QPTIFF, we need to read tiles from each channel's page
  for (size_t ch = 0; ch < num_channels; ++ch) {
    const uint16_t page = level_info.pages[ch];

    for (uint32_t tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
      for (uint32_t tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
        // Calculate tile bounds in level coordinates
        const uint32_t tile_left = tile_x * tile_width;
        const uint32_t tile_top = tile_y * tile_height;
        const uint32_t tile_right =
            std::min(tile_left + tile_width, level_info.size[0]);
        const uint32_t tile_bottom =
            std::min(tile_top + tile_height, level_info.size[1]);

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
        op.tile_coord = {static_cast<uint32_t>(ch),
                         0};  // Channel stored in X coord
        op.source_id = page;

        // For TIFF, byte_offset is the tile index
        if (is_tiled) {
          const uint32_t tiles_across =
              (level_info.size[0] + tile_width - 1) / tile_width;
          op.byte_offset = tile_y * tiles_across + tile_x;
        } else {
          op.byte_offset = tile_y;  // Strip number
        }

        // Estimate tile size
        op.byte_size = tile_width * tile_height * bytes_per_sample;

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
  }

  return operations;
}

}  // namespace fastslide
