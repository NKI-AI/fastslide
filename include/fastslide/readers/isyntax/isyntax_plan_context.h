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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_PLAN_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_PLAN_CONTEXT_H_

#include <cstdint>
#include <optional>

#include "fastslide/image.h"
#include "fastslide/slide_reader.h"

namespace fastslide {

/// @brief Read-only view of iSyntax state needed by the plan builder.
struct IsyntaxPlanContext {
  ImageDimensions tile_size = {0, 0};
  LevelInfo level_info;
  /// Padding of the wavelet pyramid on each side of level 0, in level 0
  /// pixels.
  int32_t level0_padding = 0;
  /// Start of this level's tile grid in the padded level 0, in level 0 pixels
  /// (libisyntax's `origin_offset_in_pixels`; 0 for level 0).
  int32_t origin_offset_in_pixels = 0;
  /// Size of this level's tile grid, in tiles.
  int64_t grid_width_in_tiles = 0;
  int64_t grid_height_in_tiles = 0;
  /// Whether to apply the sub-pixel correction to levels above 0. Unset:
  /// read from FASTSLIDE_ISYNTAX_SUBPIXEL_CORRECTION; see
  /// IsyntaxPlanBuilder::SubpixelCorrectionFromEnv.
  std::optional<bool> subpixel_correction;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_PLAN_CONTEXT_H_
