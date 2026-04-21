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

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/isyntax/isyntax.h"

namespace fastslide {

/// @brief Helper class to build execution plans for iSyntax tile requests
class IsyntaxPlanBuilder {
 public:
  /// @brief Build a plan for the given request
  /// @param request Tile request specifying region and level
  /// @param reader IsyntaxReader instance to access slide properties
  /// @return Result containing the execution plan or error
  static aifocore::Result<core::TilePlan> BuildPlan(
      const core::TileRequest& request, const IsyntaxReader& reader);

  /// @brief Wavelet-origin sub-pixel shift in level pixels.
  ///
  /// The iSyntax wavelet decomposition introduces padding that shifts lower
  /// resolution levels relative to level 0. Returns the fractional correction
  /// (in the coordinate space of @p level) needed to align the tile grid with
  /// the API coordinate origin.
  ///
  /// @param level Pyramid level (0 = full resolution, no shift).
  /// @param downsample Downsample factor for the level (2^scale).
  /// @return Shift in level pixels (0.0 for level 0).
  static double ComputeOriginShift(int32_t level, double downsample);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_PLAN_BUILDER_H_
