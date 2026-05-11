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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_LEVEL_INFO_H_

#include <cstdint>
#include <string>

#include "fastslide/image.h"

namespace fastslide {

/// @brief Pyramid level metadata for NDPI.
struct NdpiTiffLevelInfo {
  uint16_t page = 0;
  ImageDimensions size = {0, 0};
  double downsample_factor = 0.0;
};

/// @brief NDPI-associated image metadata (macro/map).
struct NdpiTiffAssociatedInfo {
  uint16_t page = 0;
  ImageDimensions size = {0, 0};
  std::string name;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_LEVEL_INFO_H_
