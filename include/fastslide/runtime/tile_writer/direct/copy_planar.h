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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_COPY_PLANAR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_COPY_PLANAR_H_

#include <cstdint>

#include "fastslide/runtime/tile_writer/fs_profile.h"

namespace fastslide::runtime {

/// @brief Copy planar tile data (e.g., QPTIFF spectral)
void CopyTilePlanar(const uint8_t* tile, int tile_w, int tile_h, int src_x,
                    int src_y, uint8_t* img, int img_w, int img_h, int dst_x,
                    int dst_y, int copy_w, int copy_h, int target_channel,
                    int total_channels, int bytes_per_sample);

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_COPY_PLANAR_H_
