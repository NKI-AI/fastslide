// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#include "fastslide/slide_image.h"

#include <cmath>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

aifocore::Result<core::TileRequest> SlideImage::RegionToTileRequest(
    const RegionSpec& region) const {
  if (!region.IsValid()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid region specification");
  }

  LevelInfo level_info;
  AIFOCORE_ASSIGN_OR_RETURN(level_info, GetLevelInfo(region.level));

  if (region.top_left[0] >= level_info.dimensions[0] ||
      region.top_left[1] >= level_info.dimensions[1]) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format(
            "Requested region at ({}, {}) is completely outside image "
            "bounds ({}, {}) at level {}",
            region.top_left[0], region.top_left[1], level_info.dimensions[0],
            level_info.dimensions[1], region.level));
  }

  core::TileRequest request;
  request.level = region.level;

  core::FractionalRegionBounds bounds;
  bounds.x = static_cast<double>(region.top_left[0]);
  bounds.y = static_cast<double>(region.top_left[1]);
  bounds.width = static_cast<double>(region.size[0]);
  bounds.height = static_cast<double>(region.size[1]);
  request.region_bounds = bounds;

  // Region requests do not address a specific tile cell; PrepareRequest
  // implementations use `region_bounds` to enumerate the covered tiles.
  request.tile_coord = {0, 0};
  request.channel_indices = visible_channels_;
  request.plane = region.plane;
  return request;
}

aifocore::Result<Image> SlideImage::ReadRegion(const RegionSpec& region) const {
  core::TileRequest request;
  AIFOCORE_ASSIGN_OR_RETURN(request, RegionToTileRequest(region));

  core::TilePlan plan;
  AIFOCORE_ASSIGN_OR_RETURN(plan, PrepareRequest(request));

  if (plan.output.dimensions[0] == 0 || plan.output.dimensions[1] == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("PrepareRequest returned invalid plan with zero "
                              "dimensions: [{},{}]",
                              plan.output.dimensions[0],
                              plan.output.dimensions[1]));
  }
  if (plan.output.channels == 0 || plan.output.channels > 10000) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format(
            "PrepareRequest returned invalid plan with bad channel count: {}",
            plan.output.channels));
  }

  runtime::Canvas canvas(plan);
  AIFOCORE_RETURN_IF_ERROR(ExecutePlan(plan, canvas));
  AIFOCORE_RETURN_IF_ERROR(canvas.Finalize());

  Image output;
  AIFOCORE_ASSIGN_OR_RETURN(output, canvas.GetOutput());
  return output;
}

int SlideImage::GetBestLevelForDownsample(double downsample) const {
  if (downsample <= 1.0) {
    return 0;
  }
  const int level_count = GetLevelCount();
  if (level_count <= 0) {
    return 0;
  }

  int best_level = 0;
  double best_diff = std::abs(1.0 - downsample);

  for (int level = 0; level < level_count; ++level) {
    auto level_info_result = GetLevelInfo(level);
    if (!level_info_result.ok()) {
      continue;
    }
    const double level_downsample = level_info_result.value().downsample_factor;
    const double diff = std::abs(level_downsample - downsample);
    if (diff < best_diff) {
      best_diff = diff;
      best_level = level;
    }
  }
  return best_level;
}

}  // namespace fastslide
