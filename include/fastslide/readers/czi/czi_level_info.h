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
///
/// Coordinates (`x`, `y`) are stored in absolute level-0 pixel coordinates as
/// read from the file. Per-scene origin normalisation is applied by
/// `CziSceneImage` when it builds its spatial index, not by mutating these
/// records, so a single shared subblock array can back every scene image.
struct CziSubblockInfo {
  uint32_t index = 0;       ///< Position in the reader's subblock array.
  int64_t file_pos = 0;     ///< Subblock segment offset within the file.
  int32_t pixel_type = 0;   ///< CZI pixel type code.
  int32_t compression = 0;  ///< CZI compression code.
  int32_t x = 0;            ///< Absolute X origin (level-0 pixels).
  int32_t y = 0;            ///< Absolute Y origin (level-0 pixels).
  uint32_t w = 0;           ///< Stored width in pixels.
  uint32_t h = 0;           ///< Stored height in pixels.
  int32_t scene = 0;        ///< Scene index (CZI "S" dimension start).
  int32_t z = 0;            ///< Focal-plane index (CZI "Z" dimension start).
  int32_t t = 0;            ///< Time-point index (CZI "T" dimension start).
  int32_t downsample = 1;   ///< Integer downsample factor (level-0 = 1).
  int32_t dim_count = 0;    ///< Directory entry's DimensionEntryDV count.
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_LEVEL_INFO_H_
