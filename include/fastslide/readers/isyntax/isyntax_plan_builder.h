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
#include "fastslide/core/slide_descriptor.h"
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
  /// resampling, bit-exact with the Philips SDK.
  ///
  /// @param level Pyramid level (0 = full resolution).
  /// @param level0_padding Padding of level 0, in level 0 pixels.
  /// @param origin_offset_in_pixels Start of this level's tile grid, in level
  ///        0 pixels (0 for level 0).
  /// @return Offset in pixels of @p level.
  static int64_t ComputeGridOffset(int32_t level, int32_t level0_padding,
                                   int32_t origin_offset_in_pixels);

  /// @brief Grid offset with the sub-pixel correction, in level pixels.
  ///
  /// `level0_padding / downsample` plus the wavelet-origin shift of the level
  /// (ComputeOriginShift). Equal to ComputeGridOffset at level 0; above it,
  /// it differs from it by a fraction of a pixel, so those levels are
  /// resampled. Used when FASTSLIDE_ISYNTAX_SUBPIXEL_CORRECTION=1.
  ///
  /// @param level Pyramid level (0 = full resolution).
  /// @param level0_padding Padding of level 0, in level 0 pixels.
  /// @param downsample Downsample factor of the level (2^level).
  /// @return Offset in pixels of @p level.
  static double ComputeCorrectedGridOffset(int32_t level,
                                           int32_t level0_padding,
                                           double downsample);

  /// @brief Wavelet-origin sub-pixel shift in level pixels.
  ///
  /// Aligns levels above 0 with level 0, where the Philips SDK's whole-pixel
  /// grid leaves them up to half a level pixel off. On a test slide it was
  /// within 0.05 level pixels of the shift that best matched level 0
  /// box-downsampled to the level, at levels 1 to 5.
  ///
  /// @param level Pyramid level (0 = full resolution, no shift).
  /// @param downsample Downsample factor for the level (2^level).
  /// @return Shift in level pixels (0.0 for level 0).
  static double ComputeOriginShift(int32_t level, double downsample);

  /// @brief Whether FASTSLIDE_ISYNTAX_SUBPIXEL_CORRECTION is set to "1".
  ///
  /// Read once, on first use. Used for plans whose context leaves
  /// `subpixel_correction` unset.
  static bool SubpixelCorrectionFromEnv();

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

  /// @brief Box in pixels of the padded level 0 tile grid, end excluded.
  struct GridBox {
    int64_t x0 = 0;
    int64_t y0 = 0;
    int64_t x1 = 0;
    int64_t y1 = 0;
  };

  /// @brief Slide bounds (level 0 API pixels) of @p box, clipped to the image.
  ///
  /// The Philips SDK bounds the valid data with envelopes, given in pixels of
  /// the padded level 0 grid; the API origin is `level0_padding` pixels into
  /// that grid. Returns invalid (empty) bounds when the box misses the image.
  ///
  /// @param box Union of the envelopes, in padded grid pixels.
  /// @param level0_padding Padding of level 0, in level 0 pixels.
  /// @param width Width of level 0, in pixels.
  /// @param height Height of level 0, in pixels.
  static SlideBounds GridBoxToBounds(const GridBox& box, int32_t level0_padding,
                                     int64_t width, int64_t height);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_PLAN_BUILDER_H_
