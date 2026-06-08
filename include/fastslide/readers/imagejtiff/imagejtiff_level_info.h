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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_LEVEL_INFO_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fastslide/image.h"

namespace fastslide {

/// @brief Pyramid level metadata for an ImageJ hyperstack TIFF.
///
/// ImageJ stores one TIFF page per (channel, z, t) plane at a single
/// resolution, so a level holds one page per channel for the selected
/// z/t plane. The `.pages` / `.size` members match what
/// `simpletiff_plan::BuildMultiChannelPlan` and
/// `MultiChannelTiffTileExecutor` expect.
struct ImageJTiffLevelInfo {
  std::vector<uint16_t> pages;    ///< One TIFF page per channel
  ImageDimensions size = {0, 0};  ///< Level dimensions (width, height)

  void Reserve(size_t n) { pages.reserve(n); }
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_LEVEL_INFO_H_
