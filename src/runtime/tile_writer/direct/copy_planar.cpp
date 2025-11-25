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

#include "fastslide/runtime/tile_writer/direct/copy_planar.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fastslide::runtime {

void CopyTilePlanar(const uint8_t* tile, int tile_w, int tile_h, int src_x,
                    int src_y, uint8_t* img, int img_w, int img_h, int dst_x,
                    int dst_y, int copy_w, int copy_h, int target_channel,
                    int total_channels, int bytes_per_sample) {
  const size_t pixels_per_channel = static_cast<size_t>(img_w) * img_h;
  const size_t channel_plane_offset =
      target_channel * pixels_per_channel * bytes_per_sample;

  for (int y = 0; y < copy_h; ++y) {
    const int tile_row = src_y + y;
    const int img_row = dst_y + y;

    if (tile_row >= tile_h || img_row >= img_h)
      continue;

    for (int x = 0; x < copy_w; ++x) {
      const int tile_col = src_x + x;
      const int img_col = dst_x + x;

      if (tile_col >= tile_w || img_col >= img_w)
        continue;

      const size_t src_offset =
          (tile_row * tile_w + tile_col) * bytes_per_sample;
      const size_t dst_offset =
          channel_plane_offset + (img_row * img_w + img_col) * bytes_per_sample;

      std::memcpy(img + dst_offset, tile + src_offset, bytes_per_sample);
    }
  }
}

}  // namespace fastslide::runtime
