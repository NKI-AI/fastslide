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
#include <iostream>
#include <vector>

#include <cstdint>

namespace fastslide {

namespace {

// Match the legacy iSyntax "grid origin offset" formula.
// This converts from API level coordinates into the internal tile-grid space.
//
// Legacy (also used by older wrappers/tests):
//   offset = ((3 << num_levels) - 3) >> level
int64_t ComputeGridOffsetPixels(int32_t num_levels, int32_t level) {
  if (num_levels <= 0 || level < 0) {
    return 0;
  }
  const int64_t base = (static_cast<int64_t>(3) << num_levels) - 3;
  return base >> level;
}

}  // namespace

aifocore::Result<core::TilePlan> IsyntaxPlanBuilder::BuildPlan(
    const core::TileRequest& request, const IsyntaxReader& reader) {
  core::TilePlan plan;
  plan.request = request;

  // 1. Configure output specification
  // Region bounds are fractional doubles, but we output integer pixels for now
  if (!request.region_bounds.has_value()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
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
  ImageDimensions tile_size = reader.GetTileSize();
  int32_t tile_w = static_cast<int32_t>(tile_size[0]);
  int32_t tile_h = static_cast<int32_t>(tile_size[1]);

  if (tile_w <= 0 || tile_h <= 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Invalid tile size from reader");
  }

  // Get level dimensions
  aifocore::Result<LevelInfo> level_info_or =
      reader.GetLevelInfo(request.level);
  if (!level_info_or.ok()) {
    return level_info_or.status();
  }
  const LevelInfo& level_info = *level_info_or;
  int64_t level_w = level_info.dimensions[0];
  int64_t level_h = level_info.dimensions[1];

  // 3. Calculate tile grid coverage
  // request.region_bounds.x/y are in level pixels
  double x_min = region.x;
  double y_min = region.y;
  double width = region.width;
  double height = region.height;

  // Retrieve origin offset for the requested level.
  // Centralized in the C++ libisyntax wrapper so we can reconcile/validate this
  // mapping later without touching call sites.
  int32_t num_levels = reader.GetLevelCount();
  int64_t offset = ComputeGridOffsetPixels(num_levels, request.level);

  // The offset is guaranteed to be an integer (derived from integer shifts)
  int64_t int_offset_x = static_cast<int64_t>(offset);
  int64_t int_offset_y = static_cast<int64_t>(offset);

  // 3. Calculate tile grid coverage
  // request.region_bounds.x/y are in level pixels (API space)
  // We need to map them to the underlying tile grid space to find covering
  // tiles Tile(0,0) starts at global coordinate (-int_offset_x, -int_offset_y)
  // So: GridCoordinate = ApiCoordinate + int_offset
  double grid_x_min = x_min + int_offset_x;
  double grid_y_min = y_min + int_offset_y;

  // Update actual region in plan
  // RegionSpec { top_left, size, level }
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

  int64_t max_tiles_x = (level_w + tile_w - 1) / tile_w;
  int64_t max_tiles_y = (level_h + tile_h - 1) / tile_h;

  end_tile_x = std::min<int64_t>(end_tile_x, max_tiles_x - 1);
  end_tile_y = std::min<int64_t>(end_tile_y, max_tiles_y - 1);

  // 4. Generate tile operations
  for (int64_t ty = start_tile_y; ty <= end_tile_y; ++ty) {
    for (int64_t tx = start_tile_x; tx <= end_tile_x; ++tx) {
      core::TileReadOp op;
      op.level = request.level;
      op.tile_coord = {static_cast<uint32_t>(tx), static_cast<uint32_t>(ty)};

      // These fields are unused by IsyntaxTileExecutor but good for
      // debugging/completeness
      op.source_id = 0;
      op.byte_offset = 0;
      op.byte_size = 0;

      // Calculate transform
      int64_t tile_x_px = tx * tile_w - int_offset_x;
      int64_t tile_y_px = ty * tile_h - int_offset_y;

      // Intersection with requested region (integer coords)
      int64_t req_x = static_cast<int64_t>(std::floor(x_min));
      int64_t req_y = static_cast<int64_t>(std::floor(y_min));

      // Calculate intersection of tile and request
      // We read full tiles and crop them into the output buffer
      int64_t intersect_x = std::max(tile_x_px, req_x);
      int64_t intersect_y = std::max(tile_y_px, req_y);

      int64_t req_r = req_x + static_cast<int64_t>(std::ceil(width));
      int64_t req_b = req_y + static_cast<int64_t>(std::ceil(height));

      int64_t intersect_r = std::min(tile_x_px + tile_w, req_r);
      int64_t intersect_b = std::min(tile_y_px + tile_h, req_b);

      int64_t intersect_w = std::max<int64_t>(0, intersect_r - intersect_x);
      int64_t intersect_h = std::max<int64_t>(0, intersect_b - intersect_y);

      if (intersect_w > 0 && intersect_h > 0) {
        // Source region within tile
        op.transform.source.x = static_cast<uint32_t>(intersect_x - tile_x_px);
        op.transform.source.y = static_cast<uint32_t>(intersect_y - tile_y_px);
        op.transform.source.width = static_cast<uint32_t>(intersect_w);
        op.transform.source.height = static_cast<uint32_t>(intersect_h);

        // Destination region in output
        op.transform.dest.x = static_cast<uint32_t>(intersect_x - req_x);
        op.transform.dest.y = static_cast<uint32_t>(intersect_y - req_y);
        op.transform.dest.width = static_cast<uint32_t>(intersect_w);
        op.transform.dest.height = static_cast<uint32_t>(intersect_h);

        plan.operations.push_back(op);
      }
    }
  }

  return plan;
}

}  // namespace fastslide
