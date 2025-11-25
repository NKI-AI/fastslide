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

#include "fastslide/runtime/tile_writer/direct/fills.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fastslide::runtime {

void ZeroInit(uint8_t* buf, size_t nbytes) {
  std::memset(buf, 0, nbytes);
}

void FillRGB8(uint8_t* buf, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  if (w <= 0 || h <= 0)
    return;
  const size_t total_bytes = static_cast<size_t>(w) * h * 3;

  // Fast path for uniform colors (r == g == b)
  // This handles black (0), white (255), and any gray value
  if (r == g && g == b) {
    std::memset(buf, r, total_bytes);
    return;
  }

  // Non-uniform color path (rare in medical imaging)
  // Use exponential doubling for efficiency
  const size_t row_bytes = static_cast<size_t>(w) * 3;
  uint8_t* row0 = buf;

  // Build first row via seed + exponential doubling
  const int seed_px = std::min(w, 64);  // 64 px -> 192 bytes
  for (int i = 0; i < seed_px; ++i) {
    row0[3 * i + 0] = r;
    row0[3 * i + 1] = g;
    row0[3 * i + 2] = b;
  }
  size_t filled = static_cast<size_t>(seed_px) * 3;
  while (filled < row_bytes) {
    const size_t chunk = std::min(filled, row_bytes - filled);
    std::memcpy(row0 + filled, row0, chunk);
    filled += chunk;
  }

  if (h == 1)
    return;

  // Exponential doubling across rows
  size_t filled_rows = 1;
  while (filled_rows < static_cast<size_t>(h)) {
    const size_t block_rows =
        std::min(filled_rows, static_cast<size_t>(h) - filled_rows);
    const size_t block_bytes = block_rows * row_bytes;
    std::memcpy(buf + filled_rows * row_bytes, buf, block_bytes);
    filled_rows += block_rows;
  }
}

void FillRGBA8(uint8_t* buf, int w, int h, uint8_t r, uint8_t g, uint8_t b,
               uint8_t a) {
  if (w <= 0 || h <= 0)
    return;
  const size_t total_bytes = static_cast<size_t>(w) * h * 4;

  // Fast path for uniform colors (r == g == b == a)
  if (r == g && g == b && b == a) {
    std::memset(buf, r, total_bytes);
    return;
  }

  // Non-uniform color path (rare in medical imaging)
  // Use exponential doubling for efficiency
  const size_t row_bytes = static_cast<size_t>(w) * 4;
  uint8_t* row0 = buf;

  // Build first row via seed + exponential doubling
  const int seed_px = std::min(w, 64);  // 64 px -> 256 bytes
  for (int i = 0; i < seed_px; ++i) {
    row0[4 * i + 0] = r;
    row0[4 * i + 1] = g;
    row0[4 * i + 2] = b;
    row0[4 * i + 3] = a;
  }
  size_t filled = static_cast<size_t>(seed_px) * 4;
  while (filled < row_bytes) {
    const size_t chunk = std::min(filled, row_bytes - filled);
    std::memcpy(row0 + filled, row0, chunk);
    filled += chunk;
  }

  if (h == 1)
    return;

  // Exponential doubling across rows
  size_t filled_rows = 1;
  while (filled_rows < static_cast<size_t>(h)) {
    const size_t block_rows =
        std::min(filled_rows, static_cast<size_t>(h) - filled_rows);
    const size_t block_bytes = block_rows * row_bytes;
    std::memcpy(buf + filled_rows * row_bytes, buf, block_bytes);
    filled_rows += block_rows;
  }
}

void FillGray8(uint8_t* buf, int w, int h, uint8_t value) {
  const size_t total_pixels = static_cast<size_t>(w) * h;
  std::memset(buf, value, total_pixels);
}

}  // namespace fastslide::runtime
