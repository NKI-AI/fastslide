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

#include "fastslide/readers/mrxs/mrxs_plan_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/mrxs/mrxs.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/readers/mrxs/spatial_index.h"
#include "fastslide/slide_reader.h"

namespace fastslide {

absl::StatusOr<core::TilePlan> MrxsPlanBuilder::BuildPlan(
    const core::TileRequest& request, const MrxsReader& reader) {

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

  // Get spatial index for this level
  auto index_or = reader.GetSpatialIndex(request.level);
  if (!index_or.ok()) {
    return index_or.status();
  }
  const auto& index = *index_or;

  // Determine region bounds from request
  double x, y;
  uint32_t width, height;
  DetermineRegionBounds(request, level_info, x, y, width, height);

  // Query tiles that intersect with the region
  auto tile_indices = index->QueryRegion(x, y, width, height);

  // Get slide info for output specification
  const auto& slide_info = reader.GetMrxsInfo();
  const auto& zoom_level = slide_info.zoom_levels[request.level];

  if (tile_indices.empty()) {
    // No tiles found - return empty plan
    plan.output = CreateOutputSpec(width, height, zoom_level);
    plan.actual_region = {
        .top_left = {0, 0}, .size = {width, height}, .level = request.level};
    return plan;
  }

  // Create tile operations
  auto operations = CreateTileOperations(request, *index, x, y, width, height);
  plan.operations = std::move(operations);

  // Set output specification
  plan.output = CreateOutputSpec(width, height, zoom_level);

  // Calculate cost estimates
  plan.cost = CalculateCosts(plan.operations);

  // Set actual region
  plan.actual_region = {
      .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
      .size = {width, height},
      .level = request.level};

  return plan;
}

absl::Status MrxsPlanBuilder::ValidateRequest(const core::TileRequest& request,
                                              const MrxsReader& reader) {

  if (request.level < 0 || request.level >= reader.GetLevelCount()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid level: %d", request.level));
  }

  return absl::OkStatus();
}

void MrxsPlanBuilder::DetermineRegionBounds(const core::TileRequest& request,
                                            const LevelInfo& level_info,
                                            double& x, double& y,
                                            uint32_t& width, uint32_t& height) {

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

std::vector<core::TileReadOp> MrxsPlanBuilder::CreateTileOperations(
    const core::TileRequest& request,
    const mrxs::MrxsSpatialIndex& spatial_index, double x, double y,
    uint32_t width, uint32_t height) {

  auto tile_indices = spatial_index.QueryRegion(x, y, width, height);
  const auto& spatial_tiles = spatial_index.GetSpatialTiles();

  std::vector<core::TileReadOp> operations;
  operations.reserve(tile_indices.size());

  for (size_t idx : tile_indices) {
    const auto& spatial_tile = spatial_tiles[idx];
    auto op_opt =
        CreateTileOperation(request, spatial_tile, x, y, width, height);
    if (op_opt) {
      operations.push_back(*op_opt);
    }
  }

  return operations;
}

std::optional<core::TileReadOp> MrxsPlanBuilder::CreateTileOperation(
    const core::TileRequest& request, const mrxs::SpatialTile& spatial_tile,
    double x, double y, uint32_t width, uint32_t height) {

  const auto& tile = spatial_tile.tile_info;

  core::TileReadOp op;
  op.level = request.level;
  op.tile_coord = {static_cast<uint32_t>(tile.x),
                   static_cast<uint32_t>(tile.y)};

  // Source information from tile metadata
  op.source_id = tile.data_file_number;
  op.byte_offset = tile.offset;
  op.byte_size = tile.length;

  // Calculate tile position relative to region origin
  const double tile_x_in_level = spatial_tile.bbox.min[0];
  const double tile_y_in_level = spatial_tile.bbox.min[1];
  const double rel_x = tile_x_in_level - x;
  const double rel_y = tile_y_in_level - y;

  // Extract integer and fractional components
  const int32_t dest_x = static_cast<int32_t>(std::floor(rel_x));
  const int32_t dest_y = static_cast<int32_t>(std::floor(rel_y));
  const double frac_x = rel_x - dest_x;
  const double frac_y = rel_y - dest_y;

  // Calculate initial source and destination dimensions
  uint32_t src_offset_x = static_cast<uint32_t>(std::round(tile.subregion_x));
  uint32_t src_offset_y = static_cast<uint32_t>(std::round(tile.subregion_y));
  uint32_t src_width =
      static_cast<uint32_t>(std::ceil(spatial_tile.tile_width));
  uint32_t src_height =
      static_cast<uint32_t>(std::ceil(spatial_tile.tile_height));

  uint32_t final_dest_x = 0;
  uint32_t final_dest_y = 0;
  uint32_t final_width = src_width;
  uint32_t final_height = src_height;

  // Clip left/top if tile extends before region origin
  if (dest_x < 0) {
    const uint32_t clip_amount = static_cast<uint32_t>(-dest_x);
    src_offset_x += clip_amount;
    final_width = (clip_amount < src_width) ? (src_width - clip_amount) : 0;
    final_dest_x = 0;
  } else {
    final_dest_x = static_cast<uint32_t>(dest_x);
  }

  if (dest_y < 0) {
    const uint32_t clip_amount = static_cast<uint32_t>(-dest_y);
    src_offset_y += clip_amount;
    final_height = (clip_amount < src_height) ? (src_height - clip_amount) : 0;
    final_dest_y = 0;
  } else {
    final_dest_y = static_cast<uint32_t>(dest_y);
  }

  // Clip right/bottom if tile extends beyond region bounds
  if (final_dest_x + final_width > width) {
    final_width = (width > final_dest_x) ? (width - final_dest_x) : 0;
  }
  if (final_dest_y + final_height > height) {
    final_height = (height > final_dest_y) ? (height - final_dest_y) : 0;
  }

  // Skip tiles that are completely outside the region
  if (final_width == 0 || final_height == 0) {
    return std::nullopt;
  }

  // Set transform
  op.transform.source = {src_offset_x, src_offset_y, final_width, final_height};
  op.transform.dest = {final_dest_x, final_dest_y, final_width, final_height};

  // Populate blend metadata for MRXS (supports fractional positioning +
  // averaging)
  core::BlendMetadata blend;
  blend.fractional_x = frac_x;
  blend.fractional_y = frac_y;
  blend.weight = 1.0;                      // Equal weight for all tiles
  blend.gain = tile.gain;                  // Intensity correction factor
  blend.mode = core::BlendMode::kAverage;  // MRXS uses averaging for overlaps
  blend.enable_subpixel_resampling = true;
  op.blend_metadata = blend;

  return op;
}

core::OutputSpec MrxsPlanBuilder::CreateOutputSpec(
    uint32_t width, uint32_t height, const mrxs::SlideZoomLevel& zoom_level) {

  core::OutputSpec spec;
  spec.dimensions = {width, height};
  spec.channels = 3;  // RGB
  spec.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  // TODO; This seems duplicated
  spec.background = {
      static_cast<uint8_t>((zoom_level.background_color_rgb >> 16) & 0xFF),
      static_cast<uint8_t>((zoom_level.background_color_rgb >> 8) & 0xFF),
      static_cast<uint8_t>(zoom_level.background_color_rgb & 0xFF), 255};

  return spec;
}

core::TilePlan::Cost MrxsPlanBuilder::CalculateCosts(
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
