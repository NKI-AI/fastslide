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

#include "fastslide/runtime/tile_writer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/runtime/tile_writer/bilinear_blit_hwy.h"
#include "fastslide/runtime/tile_writer/direct/copy_planar.h"
#include "fastslide/runtime/tile_writer/direct/copy_rgb8.h"
#include "fastslide/runtime/tile_writer/direct/fills.h"
#include "fastslide/runtime/tile_writer/pixel_ops.h"

namespace fastslide::runtime {

// ===========================================================================
// Construction
// ===========================================================================

void Canvas::InitOutputImage() {
  if (config_.dimensions[0] == 0 || config_.dimensions[1] == 0 ||
      config_.channels == 0 || config_.background.values.empty()) {
    throw std::invalid_argument("Invalid Canvas configuration");
  }

  out_w_ = static_cast<int>(config_.dimensions[0]);
  out_h_ = static_cast<int>(config_.dimensions[1]);

  use_rgb8_blending_ =
      (config_.channels == 3 && config_.data_type == DataType::kUInt8 &&
       config_.planar_config == PlanarConfig::kContiguous);

  use_rgb16_copy_blending_ =
      (config_.channels == 3 && config_.data_type == DataType::kUInt16 &&
       config_.planar_config == PlanarConfig::kContiguous);

  // For fluorescence slides we tag the output as kSpectral even when there
  // are 3 or 4 channels; the buffer layout is identical to RGB(A) but the
  // semantics are independent fluorophores, and downstream consumers
  // (FFI, viewers) need that distinction to route into the multi-channel
  // path.
  if (config_.force_spectral_image) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, config_.channels,
                                config_.data_type, config_.planar_config);
  } else if (config_.channels == 3) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kRGB,
                                config_.data_type, config_.planar_config);
  } else if (config_.channels == 4) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kRGBA,
                                config_.data_type, config_.planar_config);
  } else if (config_.channels == 1) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kGray,
                                config_.data_type, config_.planar_config);
  } else {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, config_.channels,
                                config_.data_type, config_.planar_config);
  }

  uint8_t* buf = output_image_->GetData();

  if (use_rgb8_blending_) {
    const uint8_t bg_r =
        !config_.background.values.empty()
            ? static_cast<uint8_t>(config_.background.values[0])
            : 255;
    const uint8_t bg_g =
        config_.background.values.size() > 1
            ? static_cast<uint8_t>(config_.background.values[1])
            : bg_r;
    const uint8_t bg_b =
        config_.background.values.size() > 2
            ? static_cast<uint8_t>(config_.background.values[2])
            : bg_r;

    FillRGB8(buf, out_w_, out_h_, bg_r, bg_g, bg_b);

    const size_t pixel_count = static_cast<size_t>(out_w_) * out_h_;
    coverage_.resize(pixel_count, 0);
  } else if (use_rgb16_copy_blending_) {
    // 16-bit MRXS fluorescence: scale the 8-bit background up by 0x101 so
    // identical RGB triplets map to the equivalent 16-bit value (Cairo /
    // lodepng convention). For arbitrary channel-specific backgrounds we
    // fall back to plain channel-0 grey.
    const auto scale8to16 = [](double v) {
      const uint16_t v8 = static_cast<uint16_t>(v);
      return static_cast<uint16_t>(v8 * 0x0101U);
    };
    const uint16_t bg_r = scale8to16(config_.background.values[0]);
    const uint16_t bg_g = config_.background.values.size() > 1
                              ? scale8to16(config_.background.values[1])
                              : bg_r;
    const uint16_t bg_b = config_.background.values.size() > 2
                              ? scale8to16(config_.background.values[2])
                              : bg_r;

    FillRGB<uint16_t>(reinterpret_cast<uint16_t*>(buf), out_w_, out_h_, bg_r,
                      bg_g, bg_b);
    const size_t pixel_count = static_cast<size_t>(out_w_) * out_h_;
    coverage_.resize(pixel_count, 0);
  } else {
    ZeroInit(buf, output_image_->SizeBytes());
  }
}

Canvas::Canvas(const core::TilePlan& plan) : config_(AnalyzePlan(plan)) {
  InitOutputImage();
}

Canvas::Canvas(const Config& config) : config_(config) {
  InitOutputImage();
}

Canvas::Canvas(ImageDimensions dimensions, BackgroundColor background,
               bool enable_blending) {
  config_.dimensions = dimensions;
  config_.channels = 3;
  config_.data_type = DataType::kUInt8;
  config_.planar_config = PlanarConfig::kContiguous;
  config_.background = std::move(background);
  config_.enable_blending = enable_blending;
  InitOutputImage();
}

// ===========================================================================
// RgbBlit / RgbBlitOffset -- integer position, direct RGB copy with coverage
// ===========================================================================

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
  if (dx0 + blit_w > out_w_)
    blit_w = out_w_ - dx0;
  if (dy0 + blit_h > out_h_)
    blit_h = out_h_ - dy0;
  if (blit_w <= 0 || blit_h <= 0)
    return;

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
      while (run < blit_w && !cov_row[run])
        ++run;
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

// ===========================================================================
// BilinearRgbBlit -- fractional position with sub-pixel precision
// ===========================================================================

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
  if (dx_start >= dx_end || dy_start >= dy_end)
    return;

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

  const int safe_dx_start = std::max(dx_start, -base_ix);
  const int safe_dx_end = std::min(dx_end, src_w - 1 - base_ix);
  const int safe_dy_start = std::max(dy_start, -base_iy);
  const int safe_dy_end = std::min(dy_end, src_h - 1 - base_iy);

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
      if (y_safe && dx >= safe_dx_start && dx < safe_dx_end)
        continue;
      if (cov_row[dx])
        continue;

      const int ix = dx + base_ix;
      const auto tl = pixel::FetchRgbPad(src, src_w, src_h, ix, iy);
      const auto tr = pixel::FetchRgbPad(src, src_w, src_h, ix + 1, iy);
      const auto bl = pixel::FetchRgbPad(src, src_w, src_h, ix, iy + 1);
      const auto br = pixel::FetchRgbPad(src, src_w, src_h, ix + 1, iy + 1);

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

// ===========================================================================
// PaintTileRgb8Blended -- RGB8 path with coverage + gain + bilinear
// ===========================================================================

aifocore::Status Canvas::PaintTileRgb8Blended(
    const core::TileReadOp& op, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, uint32_t tile_channels) {
  if (tile_channels != 3) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "RGB8-blended canvas requires 3-channel tiles");
  }

  const size_t pixel_count = static_cast<size_t>(tile_width) * tile_height;
  const size_t expected_bytes = pixel_count * 3;
  if (pixel_data.size() < expected_bytes) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Tile buffer smaller than declared dimensions");
  }

  const float gain =
      op.blend_metadata.has_value() ? op.blend_metadata->gain : 1.0F;

  const uint8_t* src = pixel_data.data();
  thread_local std::vector<uint8_t> gain_scratch;
  if (std::abs(gain - 1.0F) >= 1e-4F) {
    gain_scratch.resize(expected_bytes);
    pixel::ApplyGainLinear(src, gain_scratch.data(), pixel_count, gain);
    src = gain_scratch.data();
  }

  const double dest_x = op.transform.dest.x;
  const double dest_y = op.transform.dest.y;
  const double src_x = op.transform.source.x;
  const double src_y = op.transform.source.y;
  const int dest_w = static_cast<int>(op.transform.dest.width);
  const int dest_h = static_cast<int>(op.transform.dest.height);
  const int tw = static_cast<int>(tile_width);
  const int th = static_cast<int>(tile_height);

  const double frac_x = dest_x - std::floor(dest_x);
  const double frac_y = dest_y - std::floor(dest_y);
  const bool is_integer_pos = (frac_x < 1e-9 && frac_y < 1e-9);
  const bool needs_subtile =
      (src_x != 0.0 || src_y != 0.0 || dest_w != tw || dest_h != th);

  if (is_integer_pos && !needs_subtile) {
    RgbBlitT<uint8_t>(src, tw, th, static_cast<int>(dest_x),
                      static_cast<int>(dest_y));
  } else if (is_integer_pos && needs_subtile) {
    RgbBlitOffsetT<uint8_t>(src, tw, th, static_cast<int>(src_x),
                            static_cast<int>(src_y), dest_w, dest_h,
                            static_cast<int>(dest_x), static_cast<int>(dest_y));
  } else {
    BilinearRgbBlit(src, tw, th, dest_x, dest_y, src_x, src_y, dest_w, dest_h);
  }

  return aifocore::Status::OkStatus();
}

// ===========================================================================
// PaintTileRgb16Copy -- 16-bit RGB integer-position copy with coverage
// ===========================================================================

aifocore::Status Canvas::PaintTileRgb16Copy(const core::TileReadOp& op,
                                            std::span<const uint8_t> pixel_data,
                                            uint32_t tile_width,
                                            uint32_t tile_height,
                                            uint32_t tile_channels) {
  if (tile_channels != 3) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "RGB16-copy canvas requires 3-channel tiles");
  }

  const size_t pixel_count = static_cast<size_t>(tile_width) * tile_height;
  const size_t expected_bytes = pixel_count * 3 * sizeof(uint16_t);
  if (pixel_data.size() < expected_bytes) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Tile buffer smaller than declared 16-bit dimensions");
  }

  const uint16_t* src = reinterpret_cast<const uint16_t*>(pixel_data.data());

  // 16-bit MRXS fluorescence: integer position, no bilinear, no gain.
  // Fractional MRXS positions are floored to integer; this is intentional
  // (see "blend_mode_16bit = integer_only_16" in the design plan).
  const int dest_x = static_cast<int>(std::floor(op.transform.dest.x));
  const int dest_y = static_cast<int>(std::floor(op.transform.dest.y));
  const int src_x = static_cast<int>(std::floor(op.transform.source.x));
  const int src_y = static_cast<int>(std::floor(op.transform.source.y));
  const int dest_w = static_cast<int>(op.transform.dest.width);
  const int dest_h = static_cast<int>(op.transform.dest.height);
  const int tw = static_cast<int>(tile_width);
  const int th = static_cast<int>(tile_height);

  const bool needs_subtile =
      (src_x != 0 || src_y != 0 || dest_w != tw || dest_h != th);

  if (!needs_subtile) {
    RgbBlitT<uint16_t>(src, tw, th, dest_x, dest_y);
  } else {
    RgbBlitOffsetT<uint16_t>(src, tw, th, src_x, src_y, dest_w, dest_h, dest_x,
                             dest_y);
  }
  return aifocore::Status::OkStatus();
}

// ===========================================================================
// PaintTilePlanar -- separate channel planes (e.g., QPTIFF spectral)
// ===========================================================================

namespace {

/// @brief Clip a tile->canvas paint rectangle so neither endpoint underflows.
///
/// `TileTransform::DestRegion` stores `dest.x`/`dest.y` as doubles which can
/// be negative when a tile straddles the upper/left edge of the read region
/// (e.g. when reading a 1x1 region from inside a 256x256 tile). The legacy
/// flow truncated to `uint32_t`, which both produced an underflowed huge
/// offset and triggered an out-of-range error, silently dropping the paint.
///
/// This helper computes the integer source/destination origin and copy
/// extent that are simultaneously inside the tile and inside the canvas.
/// Returns `false` when the regions do not intersect at all (caller should
/// skip the paint without raising an error).
struct ClippedRegion {
  int src_x;
  int src_y;
  int dst_x;
  int dst_y;
  int copy_w;
  int copy_h;
};

bool ClipPaintRegion(const core::TileTransform::SourceRegion& src,
                     const core::TileTransform::DestRegion& dst, int tile_w,
                     int tile_h, int img_w, int img_h, ClippedRegion& out) {
  // Use floor for source, since src.x/src.y can be sub-pixel for MRXS tile
  // overlaps. Source coordinates are non-negative by construction (tile-
  // local subregion offsets), so a simple cast is fine.
  int src_x = static_cast<int>(std::floor(src.x));
  int src_y = static_cast<int>(std::floor(src.y));
  int dst_x = static_cast<int>(std::floor(dst.x));
  int dst_y = static_cast<int>(std::floor(dst.y));
  int copy_w = static_cast<int>(dst.width);
  int copy_h = static_cast<int>(dst.height);

  // Clip top-left: when dst is negative, advance src and shrink extent.
  if (dst_x < 0) {
    src_x -= dst_x;
    copy_w += dst_x;
    dst_x = 0;
  }
  if (dst_y < 0) {
    src_y -= dst_y;
    copy_h += dst_y;
    dst_y = 0;
  }
  if (src_x < 0) {
    dst_x -= src_x;
    copy_w += src_x;
    src_x = 0;
  }
  if (src_y < 0) {
    dst_y -= src_y;
    copy_h += src_y;
    src_y = 0;
  }

  // Clip bottom-right against both tile and canvas extents.
  copy_w = std::min(copy_w, tile_w - src_x);
  copy_w = std::min(copy_w, img_w - dst_x);
  copy_h = std::min(copy_h, tile_h - src_y);
  copy_h = std::min(copy_h, img_h - dst_y);

  if (copy_w <= 0 || copy_h <= 0) {
    return false;
  }

  out = {src_x, src_y, dst_x, dst_y, copy_w, copy_h};
  return true;
}

}  // namespace

aifocore::Status Canvas::PaintTilePlanar(const core::TileReadOp& operation,
                                         std::span<const uint8_t> pixel_data,
                                         uint32_t tile_width,
                                         uint32_t tile_height) {
  const auto& src = operation.transform.source;
  const auto& dst = operation.transform.dest;
  uint8_t* image_data = output_image_->GetData();
  const uint32_t bytes_per_sample = output_image_->GetBytesPerSample();
  const uint32_t target_channel = operation.tile_coord.x;

  if (target_channel >= config_.channels) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format(
            "Target channel {} exceeds image channel count {}", target_channel,
            config_.channels));
  }

  ClippedRegion clip;
  if (!ClipPaintRegion(src, dst, static_cast<int>(tile_width),
                       static_cast<int>(tile_height),
                       static_cast<int>(config_.dimensions[0]),
                       static_cast<int>(config_.dimensions[1]), clip)) {
    return aifocore::Status::OkStatus();
  }

  CopyTilePlanar(
      pixel_data.data(), static_cast<int>(tile_width),
      static_cast<int>(tile_height), clip.src_x, clip.src_y, image_data,
      static_cast<int>(config_.dimensions[0]),
      static_cast<int>(config_.dimensions[1]), clip.dst_x, clip.dst_y,
      clip.copy_w, clip.copy_h, static_cast<int>(target_channel),
      static_cast<int>(config_.channels), static_cast<int>(bytes_per_sample));

  return aifocore::Status::OkStatus();
}

// ===========================================================================
// PaintTileInterleaved -- generic interleaved copy (RGBA, gray, spectral)
// ===========================================================================

aifocore::Status Canvas::PaintTileInterleaved(
    const core::TileReadOp& operation, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, uint32_t tile_channels) {
  const auto& src = operation.transform.source;
  const auto& dst = operation.transform.dest;
  uint8_t* image_data = output_image_->GetData();
  const uint32_t bytes_per_sample = output_image_->GetBytesPerSample();

  ClippedRegion clip;
  if (!ClipPaintRegion(src, dst, static_cast<int>(tile_width),
                       static_cast<int>(tile_height),
                       static_cast<int>(config_.dimensions[0]),
                       static_cast<int>(config_.dimensions[1]), clip)) {
    return aifocore::Status::OkStatus();
  }

  // Fast path: full-tile copy of an RGB8 brightfield tile, with the
  // destination origin already inside the canvas (no clipping happened).
  const bool fast_path = (config_.channels == 3) && (tile_channels == 3) &&
                         (bytes_per_sample == 1) && (clip.src_x == 0) &&
                         (clip.src_y == 0) &&
                         (clip.copy_w == static_cast<int>(tile_width)) &&
                         (clip.copy_h == static_cast<int>(tile_height));

  if (fast_path) {
    CopyTileRectRGB8(pixel_data.data(), static_cast<int>(tile_width),
                     static_cast<int>(tile_height), 0, 0, image_data,
                     static_cast<int>(config_.dimensions[0]),
                     static_cast<int>(config_.dimensions[1]), clip.dst_x,
                     clip.dst_y, clip.copy_w, clip.copy_h);
    return aifocore::Status::OkStatus();
  }

  CopyRectGeneral(pixel_data.data(), static_cast<int>(tile_width),
                  static_cast<int>(tile_height),
                  static_cast<int>(tile_channels), clip.src_x, clip.src_y,
                  image_data, static_cast<int>(config_.dimensions[0]),
                  static_cast<int>(config_.dimensions[1]),
                  static_cast<int>(config_.channels), clip.dst_x, clip.dst_y,
                  clip.copy_w, clip.copy_h, static_cast<int>(bytes_per_sample));

  return aifocore::Status::OkStatus();
}

// ===========================================================================
// PaintTile dispatch
// ===========================================================================

aifocore::Status Canvas::PaintTileLocked(const core::TileReadOp& op,
                                         std::span<const uint8_t> pixel_data,
                                         uint32_t tile_width,
                                         uint32_t tile_height,
                                         uint32_t tile_channels) {
  if (!output_image_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Canvas has null image pointer");
  }

  if (use_rgb8_blending_) {
    return PaintTileRgb8Blended(op, pixel_data, tile_width, tile_height,
                                tile_channels);
  }

  if (use_rgb16_copy_blending_) {
    return PaintTileRgb16Copy(op, pixel_data, tile_width, tile_height,
                              tile_channels);
  }

  // PaintTilePlanar / PaintTileInterleaved both clip the source/dest
  // rectangles internally via ClipPaintRegion, so out-of-canvas portions of
  // a tile (negative dest origin, or destination extending past the canvas
  // border) are silently dropped instead of raised as errors. This is the
  // common case for sub-tile reads such as the 1x1 pixel inspector probe.
  if (config_.planar_config == PlanarConfig::kSeparate) {
    return PaintTilePlanar(op, pixel_data, tile_width, tile_height);
  }
  return PaintTileInterleaved(op, pixel_data, tile_width, tile_height,
                              tile_channels);
}

aifocore::Status Canvas::PaintTile(const core::TileReadOp& op,
                                   std::span<const uint8_t> tile_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   uint32_t tile_channels) {
  return PaintTileLocked(op, tile_data, tile_width, tile_height, tile_channels);
}

aifocore::Status Canvas::PaintTile(const core::TileReadOp& op,
                                   std::span<const uint8_t> tile_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   uint32_t tile_channels,
                                   std::mutex& accumulator_mutex) {
  std::lock_guard<std::mutex> lock(accumulator_mutex);
  return PaintTileLocked(op, tile_data, tile_width, tile_height, tile_channels);
}

// ===========================================================================
// FillBackground
// ===========================================================================

aifocore::Status Canvas::FillBackground(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* buffer = output_image_ ? output_image_->GetData() : nullptr;
  if (!buffer) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No output buffer available");
  }

  // For >4 channels (e.g. fluorescence with 5+ filters) we don't have a
  // meaningful 8-bit RGB(A) background, so just clear the buffer.
  if (config_.channels > 4) {
    const size_t total_bytes = static_cast<size_t>(config_.dimensions[0]) *
                               config_.dimensions[1] * config_.channels *
                               GetDataTypeSize(config_.data_type);
    std::memset(buffer, 0, total_bytes);
    return aifocore::Status::OkStatus();
  }

  // 16-bit 3-channel RGB (MRXS fluorescence): scale 8-bit background up
  // by 0x101 to keep grayscale values consistent with the InitOutputImage
  // background.
  if (config_.channels == 3 && config_.data_type == DataType::kUInt16) {
    const uint16_t r16 = static_cast<uint16_t>(r) * 0x0101U;
    const uint16_t g16 = static_cast<uint16_t>(g) * 0x0101U;
    const uint16_t b16 = static_cast<uint16_t>(b) * 0x0101U;
    FillRGB<uint16_t>(reinterpret_cast<uint16_t*>(buffer),
                      config_.dimensions[0], config_.dimensions[1], r16, g16,
                      b16);
    return aifocore::Status::OkStatus();
  }

  if (config_.channels == 3) {
    FillRGB8(buffer, config_.dimensions[0], config_.dimensions[1], r, g, b);
  } else if (config_.channels == 1) {
    const uint8_t gray = static_cast<uint8_t>((r + g + b) / 3);
    FillGray8(buffer, config_.dimensions[0], config_.dimensions[1], gray);
  } else if (config_.channels == 4) {
    FillRGBA8(buffer, config_.dimensions[0], config_.dimensions[1], r, g, b,
              255);
  } else {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("FillBackground not implemented for {} channels",
                              config_.channels));
  }

  return aifocore::Status::OkStatus();
}

// ===========================================================================
// Finalize / output
// ===========================================================================

aifocore::Status Canvas::Finalize() {
  finalized_ = true;
  return aifocore::Status::OkStatus();
}

ImageDimensions Canvas::GetDimensions() const {
  return config_.dimensions;
}

uint32_t Canvas::GetChannels() const {
  return config_.channels;
}

aifocore::Result<Image> Canvas::GetOutput() {
  if (!finalized_)
    AIFOCORE_RETURN_IF_ERROR(Finalize());
  if (!output_image_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No output image available");
  }
  return std::move(*output_image_);
}

bool Canvas::IsBlendingEnabled() const {
  return config_.enable_blending;
}

// ===========================================================================
// AnalyzePlan
// ===========================================================================

Canvas::Config Canvas::AnalyzePlan(const core::TilePlan& plan) {
  Config config;

  config.dimensions = plan.output.dimensions;

  if (config.dimensions[0] == 0 || config.dimensions[1] == 0 ||
      config.dimensions[0] > 100000 || config.dimensions[1] > 100000) {
    config.dimensions = {1, 1};
  }

  config.channels = plan.output.channels;
  if (config.channels == 0 || config.channels > 1000) {
    config.channels = 3;
  }

  switch (plan.output.pixel_format) {
    case core::OutputSpec::PixelFormat::kUInt8:
      config.data_type = DataType::kUInt8;
      break;
    case core::OutputSpec::PixelFormat::kUInt16:
      config.data_type = DataType::kUInt16;
      break;
    case core::OutputSpec::PixelFormat::kFloat32:
      config.data_type = DataType::kFloat32;
      break;
    default:
      config.data_type = DataType::kUInt8;
      break;
  }

  config.planar_config = plan.output.planar_config;
  config.background.values.clear();
  config.background.values.reserve(std::min(config.channels, 4u));

  config.background.values.push_back(
      static_cast<double>(plan.output.background.r));
  if (config.channels > 1) {
    config.background.values.push_back(
        static_cast<double>(plan.output.background.g));
  }
  if (config.channels > 2) {
    config.background.values.push_back(
        static_cast<double>(plan.output.background.b));
  }
  if (config.channels > 3) {
    config.background.values.push_back(
        static_cast<double>(plan.output.background.a));
  }

  config.enable_blending = false;
  for (const auto& op : plan.operations) {
    if (op.blend_metadata.has_value()) {
      config.enable_blending = true;
      break;
    }
  }

  config.force_spectral_image = plan.output.force_spectral_image;

  return config;
}

}  // namespace fastslide::runtime
