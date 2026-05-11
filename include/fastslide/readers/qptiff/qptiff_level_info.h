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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_LEVEL_INFO_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fastslide/image.h"

namespace fastslide {

/// @brief Pyramid level metadata for QPTIFF.
struct QpTiffLevelInfo {
  std::vector<uint16_t> pages;    ///< TIFF pages for this level
  ImageDimensions size = {0, 0};  ///< Level dimensions (width, height)
  bool tiled;                     ///< Whether all pages in this level are tiled
  bool allow_random_access;       ///< Whether this level allows random access

  void Reserve(size_t n) { pages.reserve(n); }
};

/// @brief Associated image metadata for QPTIFF.
struct QpTiffAssociatedInfo {
  uint16_t page;                  ///< TIFF page number
  ImageDimensions size = {0, 0};  ///< Image dimensions (width, height)
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_LEVEL_INFO_H_
