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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_LEVEL_INFO_H_

#include <cstdint>

namespace fastslide {

/// @brief Parsed CZI subblock metadata used by planning and execution.
struct CziSubblockInfo {
  uint32_t index = 0;
  int64_t file_pos = 0;
  int32_t pixel_type = 0;
  int32_t compression = 0;
  int32_t pyramid_type = 0;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t w = 0;
  uint32_t h = 0;
  int32_t scene = 0;
  int32_t channel = 0;
  int32_t z_index = 0;
  int32_t downsample = 1;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_LEVEL_INFO_H_
