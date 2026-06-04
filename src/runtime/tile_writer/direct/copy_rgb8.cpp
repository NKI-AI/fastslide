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

#include "fastslide/runtime/tile_writer/direct/copy_rgb8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fastslide::runtime {

void CopyTileRectRGB8(const uint8_t* tile, int tile_w, int tile_h, int src_x,
                      int src_y, uint8_t* img, int img_w, int img_h, int dst_x,
                      int dst_y, int copy_w, int copy_h) {
  const size_t row_bytes = static_cast<size_t>(copy_w) * 3;

  for (int y = 0; y < copy_h; ++y) {
    const int tile_row = src_y + y;
    const int img_row = dst_y + y;

    if (tile_row >= tile_h || img_row >= img_h)
      continue;

    const uint8_t* src = tile + (tile_row * tile_w + src_x) * 3;
    uint8_t* dst = img + (img_row * img_w + dst_x) * 3;

    std::memcpy(dst, src, row_bytes);
  }
}

void CopyRectGeneral(const uint8_t* tile, int tile_w, int tile_h,
                     int tile_channels, int src_x, int src_y, uint8_t* img,
                     int img_w, int img_h, int img_channels, int dst_x,
                     int dst_y, int copy_w, int copy_h, int bytes_per_sample) {
  const int tile_bytes_per_pixel = bytes_per_sample * tile_channels;
  const int img_bytes_per_pixel = bytes_per_sample * img_channels;
  const int copy_channels = std::min(tile_channels, img_channels);
  const size_t copy_bytes = copy_channels * bytes_per_sample;

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
          (tile_row * tile_w + tile_col) * tile_bytes_per_pixel;
      const size_t dst_offset =
          (img_row * img_w + img_col) * img_bytes_per_pixel;

      std::memcpy(img + dst_offset, tile + src_offset, copy_bytes);
    }
  }
}

}  // namespace fastslide::runtime
