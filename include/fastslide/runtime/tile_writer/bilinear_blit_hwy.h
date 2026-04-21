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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BILINEAR_BLIT_HWY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BILINEAR_BLIT_HWY_H_

#include <cstdint>

namespace fastslide::runtime {

/// @brief Highway-accelerated bilinear RGB blit for the safe interior region
///        where all 4 bilinear samples are in-bounds.
///
/// Processes rows of pixels using SIMD: loads contiguous RGB bytes from
/// the 4 sample rows, deinterleaves to R/G/B planes, does u16 weighted
/// accumulation with precomputed bilinear weights, and stores the result.
///
/// @param src         Source RGB8 tile data
/// @param src_stride  Source row stride in bytes (src_w * 3)
/// @param dst         Destination RGB8 buffer (full output image)
/// @param coverage    Coverage bitmap (1 byte per pixel, 0 = uncovered)
/// @param out_w       Output image width in pixels
/// @param dx_start    First output column to process (safe interior)
/// @param dx_end      One-past-last output column (safe interior)
/// @param dy_start    First output row to process (safe interior)
/// @param dy_end      One-past-last output row (safe interior)
/// @param base_ix     Integer part of source X offset (dx + base_ix = src ix)
/// @param base_iy     Integer part of source Y offset (dy + base_iy = src iy)
/// @param w_tl        Bilinear weight for top-left sample (0..256)
/// @param w_tr        Bilinear weight for top-right sample
/// @param w_bl        Bilinear weight for bottom-left sample
/// @param w_br        Bilinear weight for bottom-right sample
/// @param needs_interp  False if weights degenerate to nearest-neighbor
void BilinearRgbBlitHwy(const uint8_t* src, int src_stride, uint8_t* dst,
                        uint8_t* coverage, int out_w, int dx_start, int dx_end,
                        int dy_start, int dy_end, int base_ix, int base_iy,
                        int w_tl, int w_tr, int w_bl, int w_br,
                        bool needs_interp);

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BILINEAR_BLIT_HWY_H_
