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

#include "fastslide/runtime/tile_writer/bilinear_blit_hwy.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#undef HWY_TARGET_INCLUDE
#if defined(__has_include)
#if __has_include( \
    "aifo/fastslide/src/runtime/tile_writer/bilinear_blit_hwy.cpp")
#define HWY_TARGET_INCLUDE \
  "aifo/fastslide/src/runtime/tile_writer/bilinear_blit_hwy.cpp"
#elif __has_include("src/runtime/tile_writer/bilinear_blit_hwy.cpp")
#define HWY_TARGET_INCLUDE "src/runtime/tile_writer/bilinear_blit_hwy.cpp"
#else
#define HWY_TARGET_INCLUDE "bilinear_blit_hwy.cpp"
#endif
#else
#define HWY_TARGET_INCLUDE "bilinear_blit_hwy.cpp"
#endif
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();

namespace fastslide {
namespace runtime {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void BilinearRgbBlitHwyImpl(const uint8_t* src, int src_stride, uint8_t* dst,
                            uint8_t* coverage, int out_w, int dx_start,
                            int dx_end, int dy_start, int dy_end, int base_ix,
                            int base_iy, int w_tl, int w_tr, int w_bl, int w_br,
                            bool needs_interp) {
  // Work with u16 lanes for the weighted accumulation.
  const hn::ScalableTag<uint16_t> d16;
  const size_t N16 = hn::Lanes(d16);
  // We process N16 pixels at a time. Each pixel is 3 bytes in the source.
  // N16 is typically 8 (NEON 128-bit) or 16 (AVX2 256-bit).

  const auto v_w_tl = hn::Set(d16, static_cast<uint16_t>(w_tl));
  const auto v_w_tr = hn::Set(d16, static_cast<uint16_t>(w_tr));
  const auto v_w_bl = hn::Set(d16, static_cast<uint16_t>(w_bl));
  const auto v_w_br = hn::Set(d16, static_cast<uint16_t>(w_br));
  const auto v_bias = hn::Set(d16, static_cast<uint16_t>(128));

  for (int dy = dy_start; dy < dy_end; ++dy) {
    const int iy = dy + base_iy;
    uint8_t* dst_row = dst + static_cast<size_t>(dy) * out_w * 3;
    uint8_t* cov_row = coverage + static_cast<size_t>(dy) * out_w;

    // Pointers into the two source rows for this output row.
    const uint8_t* row_top = src + iy * src_stride;
    const uint8_t* row_bot = row_top + src_stride;

    int dx = dx_start;

    if (needs_interp) {
      // SIMD bilinear interpolation path.
      // Process N16 pixels at a time. For each pixel at output column dx,
      // the source column is (dx + base_ix). The 4 bilinear samples at
      // source (ix, iy), (ix+1, iy), (ix, iy+1), (ix+1, iy+1) are at
      // byte offsets ix*3, (ix+1)*3 = ix*3+3 in the respective rows.
      for (; dx + static_cast<int>(N16) <= dx_end;
           dx += static_cast<int>(N16)) {
        // Check coverage: skip chunk if all already covered.
        // Quick check: if the first byte is covered, check per-pixel.
        // For the typical first-tile case, all are uncovered.
        bool any_covered = false;
        for (size_t k = 0; k < N16; ++k) {
          if (cov_row[dx + k]) {
            any_covered = true;
            break;
          }
        }
        if (any_covered) {
          // Fall back to scalar for this chunk.
          for (size_t k = 0; k < N16; ++k) {
            const int cdx = dx + static_cast<int>(k);
            if (cov_row[cdx])
              continue;
            const int ix = cdx + base_ix;
            const uint8_t* p_tl = row_top + ix * 3;
            const uint8_t* p_tr = p_tl + 3;
            const uint8_t* p_bl = row_bot + ix * 3;
            const uint8_t* p_br = p_bl + 3;
            dst_row[cdx * 3] =
                static_cast<uint8_t>((p_tl[0] * w_tl + p_tr[0] * w_tr +
                                      p_bl[0] * w_bl + p_br[0] * w_br + 128) >>
                                     8);
            dst_row[cdx * 3 + 1] =
                static_cast<uint8_t>((p_tl[1] * w_tl + p_tr[1] * w_tr +
                                      p_bl[1] * w_bl + p_br[1] * w_br + 128) >>
                                     8);
            dst_row[cdx * 3 + 2] =
                static_cast<uint8_t>((p_tl[2] * w_tl + p_tr[2] * w_tr +
                                      p_bl[2] * w_bl + p_br[2] * w_br + 128) >>
                                     8);
            cov_row[cdx] = 255;
          }
          continue;
        }

        const int ix_base = dx + base_ix;
        const uint8_t* tl_ptr = row_top + ix_base * 3;
        const uint8_t* tr_ptr = tl_ptr + 3;
        const uint8_t* bl_ptr = row_bot + ix_base * 3;
        const uint8_t* br_ptr = bl_ptr + 3;

        // Process each channel (R, G, B) separately with stride-3 access.
        // For N16 pixels, channel c values are at offsets c, c+3, c+6, ...
        // We process per-channel to avoid the complexity of deinterleaving.
        for (int c = 0; c < 3; ++c) {
          // Gather N16 channel values from each of the 4 sample positions.
          // Since we can't do stride-3 gather efficiently, load scalar
          // and broadcast to vector. For small N16 (8-16), this is still
          // faster than the original scalar loop due to the SIMD MulAdd.
          HWY_ALIGN uint16_t tl_vals[hn::MaxLanes(d16)];
          HWY_ALIGN uint16_t tr_vals[hn::MaxLanes(d16)];
          HWY_ALIGN uint16_t bl_vals[hn::MaxLanes(d16)];
          HWY_ALIGN uint16_t br_vals[hn::MaxLanes(d16)];

          for (size_t k = 0; k < N16; ++k) {
            tl_vals[k] = tl_ptr[k * 3 + c];
            tr_vals[k] = tr_ptr[k * 3 + c];
            bl_vals[k] = bl_ptr[k * 3 + c];
            br_vals[k] = br_ptr[k * 3 + c];
          }

          const auto v_tl = hn::Load(d16, tl_vals);
          const auto v_tr = hn::Load(d16, tr_vals);
          const auto v_bl = hn::Load(d16, bl_vals);
          const auto v_br = hn::Load(d16, br_vals);

          // Weighted sum: (tl*w_tl + tr*w_tr + bl*w_bl + br*w_br + 128) >> 8
          auto sum = hn::Add(hn::Mul(v_tl, v_w_tl), hn::Mul(v_tr, v_w_tr));
          sum = hn::Add(sum,
                        hn::Add(hn::Mul(v_bl, v_w_bl), hn::Mul(v_br, v_w_br)));
          sum = hn::Add(sum, v_bias);
          const auto result = hn::ShiftRight<8>(sum);

          // Store back to interleaved RGB: scatter with stride 3.
          HWY_ALIGN uint16_t out_vals[hn::MaxLanes(d16)];
          hn::Store(result, d16, out_vals);
          for (size_t k = 0; k < N16; ++k) {
            dst_row[(dx + k) * 3 + c] = static_cast<uint8_t>(out_vals[k]);
          }
        }

        // Mark coverage.
        std::memset(cov_row + dx, 255, N16);
      }
    } else {
      // Nearest-neighbor: just memcpy rows with offset.
      const int ix_base = dx_start + base_ix;
      const int count = dx_end - dx_start;
      if (count > 0) {
        const uint8_t* src_ptr = row_top + ix_base * 3;
        // Check coverage for the whole row span at once.
        bool any_covered = false;
        for (int k = 0; k < count; ++k) {
          if (cov_row[dx_start + k]) {
            any_covered = true;
            break;
          }
        }
        if (!any_covered) {
          std::memcpy(dst_row + dx_start * 3, src_ptr, count * 3);
          std::memset(cov_row + dx_start, 255, count);
          dx = dx_end;
        }
      }
    }

    // Scalar tail for remaining pixels.
    for (; dx < dx_end; dx++) {
      if (cov_row[dx])
        continue;
      const int ix = dx + base_ix;
      const uint8_t* p_tl = row_top + ix * 3;
      if (!needs_interp) {
        dst_row[dx * 3] = p_tl[0];
        dst_row[dx * 3 + 1] = p_tl[1];
        dst_row[dx * 3 + 2] = p_tl[2];
      } else {
        const uint8_t* p_tr = p_tl + 3;
        const uint8_t* p_bl = row_bot + ix * 3;
        const uint8_t* p_br = p_bl + 3;
        dst_row[dx * 3] =
            static_cast<uint8_t>((p_tl[0] * w_tl + p_tr[0] * w_tr +
                                  p_bl[0] * w_bl + p_br[0] * w_br + 128) >>
                                 8);
        dst_row[dx * 3 + 1] =
            static_cast<uint8_t>((p_tl[1] * w_tl + p_tr[1] * w_tr +
                                  p_bl[1] * w_bl + p_br[1] * w_br + 128) >>
                                 8);
        dst_row[dx * 3 + 2] =
            static_cast<uint8_t>((p_tl[2] * w_tl + p_tr[2] * w_tr +
                                  p_bl[2] * w_bl + p_br[2] * w_br + 128) >>
                                 8);
      }
      cov_row[dx] = 255;
    }
  }
}

}  // namespace HWY_NAMESPACE
}  // namespace runtime
}  // namespace fastslide

HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fastslide {
namespace runtime {

HWY_EXPORT(BilinearRgbBlitHwyImpl);

void BilinearRgbBlitHwy(const uint8_t* src, int src_stride, uint8_t* dst,
                        uint8_t* coverage, int out_w, int dx_start, int dx_end,
                        int dy_start, int dy_end, int base_ix, int base_iy,
                        int w_tl, int w_tr, int w_bl, int w_br,
                        bool needs_interp) {
  HWY_DYNAMIC_DISPATCH(BilinearRgbBlitHwyImpl)
  (src, src_stride, dst, coverage, out_w, dx_start, dx_end, dy_start, dy_end,
   base_ix, base_iy, w_tl, w_tr, w_bl, w_br, needs_interp);
}

}  // namespace runtime
}  // namespace fastslide
#endif  // HWY_ONCE
