// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#include "fastslide/runtime/tile_writer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fastslide/runtime/tile_writer/bilinear_blit_hwy.h"
#include "fastslide/runtime/tile_writer/pixel_ops.h"

namespace fastslide::runtime {

template <typename PixelT>
void Canvas::RgbBlitT(const PixelT* src, int src_w, int src_h, int dest_x,
                      int dest_y) {
  RgbBlitOffsetT<PixelT>(src, src_w, src_h, 0, 0, src_w, src_h, dest_x, dest_y);
}

template <typename PixelT>
void Canvas::RgbBlitOffsetT(const PixelT* src, int src_w, int /*src_h*/,
                            int src_off_x, int src_off_y, int blit_w,
                            int blit_h, int dest_x, int dest_y) {
  int sx0 = src_off_x;
  int sy0 = src_off_y;
  int dx0 = dest_x;
  int dy0 = dest_y;

  if (dx0 < 0) {
    sx0 -= dx0;
    blit_w += dx0;
    dx0 = 0;
  }
  if (dy0 < 0) {
    sy0 -= dy0;
    blit_h += dy0;
    dy0 = 0;
  }
  if (dx0 + blit_w > out_w_) {
    blit_w = out_w_ - dx0;
  }
  if (dy0 + blit_h > out_h_) {
    blit_h = out_h_ - dy0;
  }
  if (blit_w <= 0 || blit_h <= 0) {
    return;
  }

  PixelT* buf = reinterpret_cast<PixelT*>(output_image_->GetData());
  const int src_stride = src_w * 3;

  for (int row = 0; row < blit_h; ++row) {
    const PixelT* src_row = src + (sy0 + row) * src_stride + sx0 * 3;
    PixelT* dst_row = buf + (dy0 + row) * out_w_ * 3 + dx0 * 3;
    uint8_t* cov_row = coverage_.data() + (dy0 + row) * out_w_ + dx0;

    int col = 0;
    while (col < blit_w) {
      if (cov_row[col]) {
        ++col;
        continue;
      }
      int run = col;
      while (run < blit_w && !cov_row[run]) {
        ++run;
      }
      const int len = run - col;
      std::memcpy(dst_row + col * 3, src_row + col * 3,
                  static_cast<size_t>(len) * 3 * sizeof(PixelT));
      std::memset(cov_row + col, 255, len);
      col = run;
    }
  }
}

// Explicit instantiations: the templated blit is defined in this TU and used
// from PaintTileRgb8Blended (uint8_t) and PaintTileRgb16Copy (uint16_t).
template void Canvas::RgbBlitT<uint8_t>(const uint8_t*, int, int, int, int);
template void Canvas::RgbBlitT<uint16_t>(const uint16_t*, int, int, int, int);
template void Canvas::RgbBlitOffsetT<uint8_t>(const uint8_t*, int, int, int,
                                              int, int, int, int, int);
template void Canvas::RgbBlitOffsetT<uint16_t>(const uint16_t*, int, int, int,
                                               int, int, int, int, int);

void Canvas::BilinearRgbBlit(const uint8_t* src, int src_w, int src_h,
                             double dest_x, double dest_y, double src_offset_x,
                             double src_offset_y, int visible_w,
                             int visible_h) {
  const int dx_start = std::max(static_cast<int>(std::floor(dest_x)), 0);
  const int dy_start = std::max(static_cast<int>(std::floor(dest_y)), 0);
  const int dx_end =
      std::min(static_cast<int>(std::ceil(dest_x + visible_w)), out_w_);
  const int dy_end =
      std::min(static_cast<int>(std::ceil(dest_y + visible_h)), out_h_);
  if (dx_start >= dx_end || dy_start >= dy_end) {
    return;
  }

  const double base_fx = -dest_x + src_offset_x;
  const double base_fy = -dest_y + src_offset_y;
  const int base_ix = static_cast<int>(std::floor(base_fx));
  const int base_iy = static_cast<int>(std::floor(base_fy));
  const double frac_x = base_fx - base_ix;
  const double frac_y = base_fy - base_iy;
  const int distx = static_cast<int>(frac_x * 16.0 + 0.5);
  const int disty = static_cast<int>(frac_y * 16.0 + 0.5);
  const int w_tl = (16 - distx) * (16 - disty);
  const int w_tr = distx * (16 - disty);
  const int w_bl = (16 - distx) * disty;
  const int w_br = distx * disty;
  const bool needs_interp = (distx != 0 || disty != 0);

  // Confine sampling to the source sub-rectangle, not the whole physical tile.
  // At level > 0 a physical tile packs d*d independently-downsampled level-0
  // tiles side by side; the sub-cells image scene positions ~overlap apart, so
  // sampling ix+1 / iy+1 past a sub-cell edge would read a *neighbouring*
  // tile's pixels and paint a bright/coloured line. Clamping ix/iy to the
  // sub-rect (replicate the border pixel itself, never border+1) makes each
  // tile resample as an isolated image. Tiles still land on the common integer
  // output grid, so overlaps remain seamless.
  const int sub_x0 = std::max(0, static_cast<int>(src_offset_x));
  const int sub_y0 = std::max(0, static_cast<int>(src_offset_y));
  const int sub_x1 =
      std::min(src_w, static_cast<int>(src_offset_x) + visible_w);
  const int sub_y1 =
      std::min(src_h, static_cast<int>(src_offset_y) + visible_h);

  const int safe_dx_start = std::max(dx_start, sub_x0 - base_ix);
  const int safe_dx_end = std::min(dx_end, sub_x1 - 1 - base_ix);
  const int safe_dy_start = std::max(dy_start, sub_y0 - base_iy);
  const int safe_dy_end = std::min(dy_end, sub_y1 - 1 - base_iy);

  const int src_stride = src_w * 3;
  uint8_t* buf = output_image_->GetData();

  if (safe_dx_start < safe_dx_end && safe_dy_start < safe_dy_end) {
    BilinearRgbBlitHwy(src, src_stride, buf, coverage_.data(), out_w_,
                       safe_dx_start, safe_dx_end, safe_dy_start, safe_dy_end,
                       base_ix, base_iy, w_tl, w_tr, w_bl, w_br, needs_interp);
  }

  for (int dy = dy_start; dy < dy_end; ++dy) {
    const int iy = dy + base_iy;
    uint8_t* dst_row = buf + static_cast<size_t>(dy) * out_w_ * 3;
    uint8_t* cov_row = coverage_.data() + static_cast<size_t>(dy) * out_w_;
    const bool y_safe = (dy >= safe_dy_start && dy < safe_dy_end);

    for (int dx = dx_start; dx < dx_end; ++dx) {
      if (y_safe && dx >= safe_dx_start && dx < safe_dx_end) {
        continue;
      }
      if (cov_row[dx]) {
        continue;
      }

      const int ix = dx + base_ix;
      // Clamp to the sub-rectangle so border samples replicate the sub-tile's
      // own edge instead of bleeding in the neighbouring packed tile.
      const auto fetch = [&](int x, int y) -> pixel::Rgb8 {
        x = std::clamp(x, sub_x0, sub_x1 - 1);
        y = std::clamp(y, sub_y0, sub_y1 - 1);
        const size_t off = (static_cast<size_t>(y) * src_w + x) * 3;
        return {src[off], src[off + 1], src[off + 2]};
      };
      const auto tl = fetch(ix, iy);
      const auto tr = fetch(ix + 1, iy);
      const auto bl = fetch(ix, iy + 1);
      const auto br = fetch(ix + 1, iy + 1);

      if (!needs_interp) {
        dst_row[dx * 3] = tl.r;
        dst_row[dx * 3 + 1] = tl.g;
        dst_row[dx * 3 + 2] = tl.b;
      } else {
        dst_row[dx * 3] = static_cast<uint8_t>(
            (tl.r * w_tl + tr.r * w_tr + bl.r * w_bl + br.r * w_br + 128) >> 8);
        dst_row[dx * 3 + 1] = static_cast<uint8_t>(
            (tl.g * w_tl + tr.g * w_tr + bl.g * w_bl + br.g * w_br + 128) >> 8);
        dst_row[dx * 3 + 2] = static_cast<uint8_t>(
            (tl.b * w_tl + tr.b * w_tr + bl.b * w_bl + br.b * w_br + 128) >> 8);
      }
      cov_row[dx] = 255;
    }
  }
}

}  // namespace fastslide::runtime
