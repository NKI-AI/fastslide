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

#ifndef AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_PLAN_BUILDER_H_
#define AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_PLAN_BUILDER_H_

#include <cstdint>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/isyntax/isyntax_plan_context.h"

namespace fastslide {

/// @brief Helper class to build execution plans for iSyntax tile requests
class IsyntaxPlanBuilder {
 public:
  /// @brief Build a plan for the given request
  /// @param request Tile request specifying region and level
  /// @param context Read-only planning context
  /// @return Result containing the execution plan or error
  static aifocore::Result<core::TilePlan> BuildPlan(
      const core::TileRequest& request, const IsyntaxPlanContext& context);

  /// @brief Offset from the API origin to the tile grid, in level pixels.
  ///
  /// The iSyntax wavelet pyramid is padded by `level0_padding` pixels on each
  /// side of level 0, and each decoded level's tile grid starts
  /// `origin_offset_in_pixels` (level 0 pixels) into that padding. The API
  /// origin is the top-left of the unpadded image, which is the convention of
  /// the Philips SDK. The offset is an integer number of level pixels: pixel
  /// `x` of a level is grid pixel `x + offset`, so tiles are copied without
  /// resampling.
  ///
  /// @param level Pyramid level (0 = full resolution).
  /// @param level0_padding Padding of level 0, in level 0 pixels.
  /// @param origin_offset_in_pixels Start of this level's tile grid, in level
  ///        0 pixels (0 for level 0).
  /// @return Offset in pixels of @p level.
  static int64_t ComputeGridOffset(int32_t level, int32_t level0_padding,
                                   int32_t origin_offset_in_pixels);

  /// @brief Valid pixels of a level, in Philips SDK pixels of that level.
  struct ValidRange {
    /// First valid pixel, on both axes.
    int64_t start = 0;
    /// Last valid pixel (included).
    int64_t end_x = 0;
    int64_t end_y = 0;
  };

  /// @brief Range of @p level the Philips SDK reports as valid
  /// (`DimensionRanges`).
  ///
  /// Level 0 is valid whole. Each wavelet level halves the range of the level
  /// above and loses one more pixel on each side: `start = ceil(start / 2) +
  /// 1`, `end = floor(end / 2) - 1`. The reader exposes every level as this
  /// range: its pixel 0 is the first valid SDK pixel and its dimensions are
  /// the size of the range.
  ///
  /// @param level Pyramid level (0 = full resolution).
  /// @param width Width of level 0, in pixels.
  /// @param height Height of level 0, in pixels.
  static ValidRange ComputeValidRange(int32_t level, int64_t width,
                                      int64_t height);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_PLAN_BUILDER_H_
