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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_LEVEL_INFO_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fastslide/image.h"

namespace fastslide {

/// @brief Pyramid level mapping for a multi-dimensional OME-TIFF level.
///
/// `pages` is the channel page list for the *currently selected* (Z, T) plane;
/// the read pipeline (`BuildMultiChannelPlan`) indexes it by channel. The full
/// (Z, T, C) page table lives in `plane_pages`, laid out plane-major as
/// `((z * t_count) + t) * channel_count + c`. For plain 2D OME-TIFFs
/// (`z_count == t_count == 1`) `pages` and `plane_pages` are identical.
struct OmeTiffLevelInfo {
  std::vector<uint32_t> pages;    ///< Channel pages for the selected plane.
  ImageDimensions size = {0, 0};  ///< Level dimensions
  bool tiled = false;
  bool allow_random_access = false;

  // Full (Z, T, C) plane table for this level.
  std::vector<uint32_t> plane_pages;  ///< Plane-major page table.
  uint32_t z_count = 1;               ///< Focal planes at this level.
  uint32_t t_count = 1;               ///< Time points at this level.
  uint32_t channel_count = 1;         ///< Channels per plane.

  void Reserve(size_t n) { pages.reserve(n); }

  /// @brief Channel pages for plane (z, t), or empty if out of range.
  [[nodiscard]] std::span<const uint32_t> PagesForPlane(uint32_t z,
                                                        uint32_t t) const {
    if (z >= z_count || t >= t_count || channel_count == 0) {
      return {};
    }
    const size_t base = (static_cast<size_t>(z) * t_count + t) * channel_count;
    if (base + channel_count > plane_pages.size()) {
      return {};
    }
    return std::span<const uint32_t>(plane_pages.data() + base, channel_count);
  }
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_LEVEL_INFO_H_
