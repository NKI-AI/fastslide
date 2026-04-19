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

#include "fastslide/readers/czi/czi_plan_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/czi/czi.h"
#include "fastslide/readers/czi/czi_spatial_index.h"

namespace fastslide {

namespace {

aifocore::Status ValidateRequest(const core::TileRequest& request,
                                 const CziReader& reader) {
  if (request.level < 0 || request.level >= reader.GetLevelCount()) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }
  return aifocore::Status::OkStatus();
}

void DetermineRegionBounds(const core::TileRequest& request,
                           const LevelInfo& level_info, double& x, double& y,
                           uint32_t& width, uint32_t& height) {
  if (request.IsRegionRequest() && request.region_bounds->IsValid()) {
    x = request.region_bounds->x;
    y = request.region_bounds->y;
    width = static_cast<uint32_t>(std::ceil(request.region_bounds->width));
    height = static_cast<uint32_t>(std::ceil(request.region_bounds->height));
  } else {
    x = 0.0;
    y = 0.0;
    width = level_info.dimensions[0];
    height = level_info.dimensions[1];
  }
}

core::OutputSpec CreateOutputSpec(uint32_t width, uint32_t height) {
  core::OutputSpec spec;
  spec.dimensions = {width, height};
  spec.channels = 3;  // RGB
  spec.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  spec.background = {255, 255, 255, 255};
  return spec;
}

std::optional<core::TileReadOp> CreateTileOperation(
    int level, uint32_t subblock_index, const czi::SpatialTile& spatial_tile,
    double region_x, double region_y, uint32_t region_w, uint32_t region_h) {
  const double tile_x_in_level = spatial_tile.bbox.min[0];
  const double tile_y_in_level = spatial_tile.bbox.min[1];

  const double rel_x = tile_x_in_level - region_x;
  const double rel_y = tile_y_in_level - region_y;

  const int32_t dest_x = static_cast<int32_t>(std::floor(rel_x));
  const int32_t dest_y = static_cast<int32_t>(std::floor(rel_y));
  const double frac_x = rel_x - dest_x;
  const double frac_y = rel_y - dest_y;

  const uint32_t tile_w = spatial_tile.info.width;
  const uint32_t tile_h = spatial_tile.info.height;

  uint32_t src_offset_x = 0;
  uint32_t src_offset_y = 0;
  uint32_t final_dest_x = 0;
  uint32_t final_dest_y = 0;
  uint32_t final_w = tile_w;
  uint32_t final_h = tile_h;

  if (dest_x < 0) {
    const uint32_t clip = static_cast<uint32_t>(-dest_x);
    src_offset_x += clip;
    final_w = (clip < tile_w) ? (tile_w - clip) : 0;
    final_dest_x = 0;
  } else {
    final_dest_x = static_cast<uint32_t>(dest_x);
  }

  if (dest_y < 0) {
    const uint32_t clip = static_cast<uint32_t>(-dest_y);
    src_offset_y += clip;
    final_h = (clip < tile_h) ? (tile_h - clip) : 0;
    final_dest_y = 0;
  } else {
    final_dest_y = static_cast<uint32_t>(dest_y);
  }

  if (final_dest_x + final_w > region_w) {
    final_w = (region_w > final_dest_x) ? (region_w - final_dest_x) : 0;
  }
  if (final_dest_y + final_h > region_h) {
    final_h = (region_h > final_dest_y) ? (region_h - final_dest_y) : 0;
  }

  if (final_w == 0 || final_h == 0) {
    return std::nullopt;
  }

  core::TileReadOp op{};
  op.level = level;
  // For CZI, tile_coord is not a grid index. We use the subblock index as a
  // stable unique identifier for caching/debugging.
  op.tile_coord = {subblock_index, 0};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 0;

  op.transform.source = {src_offset_x, src_offset_y, final_w, final_h};
  op.transform.dest = {final_dest_x, final_dest_y, final_w, final_h};

  core::BlendMetadata blend{};
  blend.fractional_x = frac_x;
  blend.fractional_y = frac_y;
  blend.weight = 1.0;
  blend.gain = 1.0f;
  blend.mode = core::BlendMode::kAverage;
  blend.enable_subpixel_resampling = true;
  op.blend_metadata = blend;

  return op;
}

}  // namespace

aifocore::Result<core::TilePlan> CziPlanBuilder::BuildPlan(
    const core::TileRequest& request, const CziReader& reader) {
  core::TilePlan plan;
  plan.request = request;

  AIFOCORE_RETURN_IF_ERROR(ValidateRequest(request, reader));
  AIFOCORE_ASSIGN_OR_RETURN(const auto level_info,
                            reader.GetLevelInfo(request.level));

  double x = 0.0;
  double y = 0.0;
  uint32_t width = 0;
  uint32_t height = 0;
  DetermineRegionBounds(request, level_info, x, y, width, height);

  // Empty regions still return a plan (filled with background).
  if (width == 0 || height == 0) {
    plan.output = CreateOutputSpec(width, height);
    plan.actual_region = {
        .top_left = {0, 0}, .size = {width, height}, .level = request.level};
    return plan;
  }

  // Clamp region to level bounds.
  if (x >= static_cast<double>(level_info.dimensions[0]) ||
      y >= static_cast<double>(level_info.dimensions[1])) {
    plan.output = CreateOutputSpec(width, height);
    plan.actual_region = {
        .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
        .size = {width, height},
        .level = request.level};
    return plan;
  }
  if (x + width > level_info.dimensions[0]) {
    width = level_info.dimensions[0] - static_cast<uint32_t>(x);
  }
  if (y + height > level_info.dimensions[1]) {
    height = level_info.dimensions[1] - static_cast<uint32_t>(y);
  }

  // Get spatial index for this level.
  const auto index_or = reader.GetSpatialIndex(request.level);
  if (!index_or.ok()) {
    return index_or.status();
  }
  const auto& index = *index_or;

  auto tile_indices = index->QueryRegion(x, y, width, height);
  const auto& tiles = index->GetTiles();

  std::vector<core::TileReadOp> ops;
  ops.reserve(tile_indices.size());
  for (size_t idx : tile_indices) {
    const auto& st = tiles[idx];
    const uint32_t subblock_index = st.info.subblock_index;
    auto op = CreateTileOperation(request.level, subblock_index, st, x, y,
                                  width, height);
    if (op) {
      ops.push_back(*op);
    }
  }

  plan.operations = std::move(ops);
  plan.output = CreateOutputSpec(width, height);
  plan.actual_region = {
      .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
      .size = {width, height},
      .level = request.level};

  return plan;
}

}  // namespace fastslide
