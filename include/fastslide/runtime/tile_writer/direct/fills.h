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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_FILLS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_FILLS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fastslide/runtime/tile_writer/fs_profile.h"

namespace fastslide::runtime {

void ZeroInit(uint8_t* buf, size_t nbytes);

/// @brief Fill a width*height RGB buffer of `PixelT` samples with (r,g,b).
///
/// Uses the same exponential-doubling fast path as the legacy 8-bit
/// `FillRGB8` helper, which keeps the brightfield startup cost identical
/// while making the routine reusable for 16-bit fluorescence Canvases.
template <typename PixelT>
void FillRGB(PixelT* buf, int w, int h, PixelT r, PixelT g, PixelT b) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const std::size_t row_pixels = static_cast<std::size_t>(w) * 3U;
  const std::size_t row_bytes = row_pixels * sizeof(PixelT);
  const std::size_t total_pixels = row_pixels * static_cast<std::size_t>(h);
  const std::size_t total_bytes = total_pixels * sizeof(PixelT);

  // Uniform grayscale fast path -- only safe for byte-sized samples,
  // since std::memset writes individual bytes.
  if constexpr (sizeof(PixelT) == 1U) {
    if (r == g && g == b) {
      std::memset(buf, static_cast<int>(r), total_bytes);
      return;
    }
  }

  // Build first row by hand, then exponentially double.
  const int seed_px = std::min(w, 64);
  for (int i = 0; i < seed_px; ++i) {
    buf[3 * i + 0] = r;
    buf[3 * i + 1] = g;
    buf[3 * i + 2] = b;
  }
  std::size_t filled_pixels = static_cast<std::size_t>(seed_px) * 3U;
  while (filled_pixels < row_pixels) {
    const std::size_t chunk_pixels =
        std::min(filled_pixels, row_pixels - filled_pixels);
    std::memcpy(buf + filled_pixels, buf, chunk_pixels * sizeof(PixelT));
    filled_pixels += chunk_pixels;
  }

  if (h == 1) {
    return;
  }

  std::size_t filled_rows = 1;
  while (filled_rows < static_cast<std::size_t>(h)) {
    const std::size_t block_rows =
        std::min(filled_rows, static_cast<std::size_t>(h) - filled_rows);
    std::memcpy(buf + filled_rows * row_pixels, buf, block_rows * row_bytes);
    filled_rows += block_rows;
  }
}

/// @brief Legacy 8-bit RGB fill. Equivalent to `FillRGB<uint8_t>`.
void FillRGB8(uint8_t* buf, int w, int h, uint8_t r, uint8_t g, uint8_t b);

void FillRGBA8(uint8_t* buf, int w, int h, uint8_t r, uint8_t g, uint8_t b,
               uint8_t a);

void FillGray8(uint8_t* buf, int w, int h, uint8_t value);

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_FILLS_H_
