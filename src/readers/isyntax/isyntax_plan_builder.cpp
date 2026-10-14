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

#include "fastslide/readers/isyntax/isyntax_plan_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fastslide {

int64_t IsyntaxPlanBuilder::ComputeGridOffset(int32_t level,
                                              int32_t level0_padding,
                                              int32_t origin_offset_in_pixels) {
  // Floor division: the Philips SDK samples level pixels on the integer grid
  // of the level, so the fractional part of the offset is dropped.
  return (static_cast<int64_t>(level0_padding) - origin_offset_in_pixels) >>
         level;
}

aifocore::Result<core::TilePlan> IsyntaxPlanBuilder::BuildPlan(
    const core::TileRequest& request, const IsyntaxPlanContext& context) {
  core::TilePlan plan;
  plan.request = request;

  // 1. Configure output specification
  // Region bounds are fractional doubles, but we output integer pixels for now
  if (!request.region_bounds.has_value()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Region bounds not specified");
  }

  const auto& region = *request.region_bounds;
  plan.output.dimensions[0] = static_cast<uint32_t>(std::ceil(region.width));
  plan.output.dimensions[1] = static_cast<uint32_t>(std::ceil(region.height));

  // Set format properties
  plan.output.pixel_format = OutputSpec::PixelFormat::kUInt8;
  plan.output.planar_config = PlanarConfig::kContiguous;  // RGB packed
  plan.output.channels = 3;                               // RGB
  plan.output.channel_indices = request.channel_indices.empty()
                                    ? std::vector<size_t>{0, 1, 2}
                                    : request.channel_indices;

  // 2. Get geometry information
  ImageDimensions tile_size = context.tile_size;
  int32_t tile_w = static_cast<int32_t>(tile_size[0]);
  int32_t tile_h = static_cast<int32_t>(tile_size[1]);

  if (tile_w <= 0 || tile_h <= 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Invalid tile size from reader");
  }

  // 3. Calculate tile grid coverage
  // request.region_bounds.x/y are in level pixels (API space). The tile grid
  // is offset from it by a whole number of level pixels:
  //   grid_coord = api_coord + offset
  double x_min = region.x;
  double y_min = region.y;
  double width = region.width;
  double height = region.height;

  const auto offset = static_cast<double>(context.grid_offset);
  double offset_x = offset;
  double offset_y = offset;

  double grid_x_min = x_min + offset_x;
  double grid_y_min = y_min + offset_y;

  // Update actual region in plan
  plan.actual_region.top_left[0] = static_cast<uint32_t>(region.x);
  plan.actual_region.top_left[1] = static_cast<uint32_t>(region.y);
  plan.actual_region.size[0] = static_cast<uint32_t>(region.width);
  plan.actual_region.size[1] = static_cast<uint32_t>(region.height);
  plan.actual_region.level = request.level;

  // Calculate start/end tile indices in the grid
  int64_t start_tile_x = static_cast<int64_t>(std::floor(grid_x_min / tile_w));
  int64_t start_tile_y = static_cast<int64_t>(std::floor(grid_y_min / tile_h));
  int64_t end_tile_x =
      static_cast<int64_t>(std::floor((grid_x_min + width - 1e-6) / tile_w));
  int64_t end_tile_y =
      static_cast<int64_t>(std::floor((grid_y_min + height - 1e-6) / tile_h));

  // Clamp to slide boundaries
  start_tile_x = std::max<int64_t>(0, start_tile_x);
  start_tile_y = std::max<int64_t>(0, start_tile_y);

  int64_t max_tiles_x = context.grid_width_in_tiles;
  int64_t max_tiles_y = context.grid_height_in_tiles;

  end_tile_x = std::min<int64_t>(end_tile_x, max_tiles_x - 1);
  end_tile_y = std::min<int64_t>(end_tile_y, max_tiles_y - 1);

  // 4. Generate tile operations
  // Tile positions in API space are whole pixels:
  //   tile_api_pos = tile_grid_pos - offset
  // so the Canvas copies tiles without resampling.
  for (int64_t ty = start_tile_y; ty <= end_tile_y; ++ty) {
    for (int64_t tx = start_tile_x; tx <= end_tile_x; ++tx) {
      core::TileReadOp op;
      op.level = request.level;
      op.tile_coord = {static_cast<uint32_t>(tx), static_cast<uint32_t>(ty)};

      op.source_id = 0;
      op.byte_offset = 0;
      op.byte_size = 0;

      // Tile position in API (level-pixel) space.
      double tile_x_px = tx * tile_w - offset_x;
      double tile_y_px = ty * tile_h - offset_y;

      // Intersect the tile rectangle with the requested region.
      double intersect_x = std::max(tile_x_px, x_min);
      double intersect_y = std::max(tile_y_px, y_min);

      double intersect_r = std::min(tile_x_px + tile_w, x_min + width);
      double intersect_b = std::min(tile_y_px + tile_h, y_min + height);

      double intersect_w = std::max(0.0, intersect_r - intersect_x);
      double intersect_h = std::max(0.0, intersect_b - intersect_y);

      if (intersect_w > 0 && intersect_h > 0) {
        auto visible_w =
            static_cast<uint32_t>(std::ceil(intersect_r - intersect_x));
        auto visible_h =
            static_cast<uint32_t>(std::ceil(intersect_b - intersect_y));

        op.transform.source.x = intersect_x - tile_x_px;
        op.transform.source.y = intersect_y - tile_y_px;
        op.transform.source.width = visible_w;
        op.transform.source.height = visible_h;

        op.transform.dest.x = intersect_x - x_min;
        op.transform.dest.y = intersect_y - y_min;
        op.transform.dest.width = visible_w;
        op.transform.dest.height = visible_h;

        plan.operations.push_back(op);
      }
    }
  }

  return plan;
}

}  // namespace fastslide
