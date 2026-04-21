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
    return AIFOCORE_MAKE_STATUS(
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

  const double dest_x = tile_x_in_level - region_x;
  const double dest_y = tile_y_in_level - region_y;

  const uint32_t tile_w = spatial_tile.info.width;
  const uint32_t tile_h = spatial_tile.info.height;

  if (dest_x + tile_w <= 0.0 || dest_y + tile_h <= 0.0 || dest_x >= region_w ||
      dest_y >= region_h) {
    return std::nullopt;
  }

  core::TileReadOp op{};
  op.level = level;
  op.tile_coord = {subblock_index, 0};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 0;

  op.transform.source = {0.0, 0.0, tile_w, tile_h};
  op.transform.dest = {dest_x, dest_y, tile_w, tile_h};

  core::BlendMetadata blend{};
  blend.weight = 1.0;
  blend.gain = 1.0f;
  blend.mode = core::BlendMode::kAverage;
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
  AIFOCORE_ASSIGN_OR_RETURN(const auto& index,
                            reader.GetSpatialIndex(request.level));

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
