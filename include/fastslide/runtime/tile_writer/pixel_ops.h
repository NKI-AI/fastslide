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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_PIXEL_OPS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_PIXEL_OPS_H_

/// @file pixel_ops.h
/// @brief Inline RGB pixel operations: bilinear interpolation and gain
///        correction.  All operations stay in RGB -- no ARGB32 conversion.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fastslide::pixel {

// ---------------------------------------------------------------------------
// sRGB <-> linear conversion for gain correction
// ---------------------------------------------------------------------------

/// sRGB to linear: decompand a single [0..255] channel to [0..1] float.
inline float SrgbToLinear(uint8_t v) {
  const float s = static_cast<float>(v) / 255.0F;
  return (s <= 0.04045F) ? (s / 12.92F) : std::pow((s + 0.055F) / 1.055F, 2.4F);
}

/// Linear to sRGB: compand a [0..1] float to [0..255] uint8.
inline uint8_t LinearToSrgb(float v) {
  v = std::clamp(v, 0.0F, 1.0F);
  const float s = (v <= 0.0031308F)
                      ? (v * 12.92F)
                      : (1.055F * std::pow(v, 1.0F / 2.4F) - 0.055F);
  return static_cast<uint8_t>(s * 255.0F + 0.5F);
}

// ---------------------------------------------------------------------------
// Packed RGB triplet
// ---------------------------------------------------------------------------

struct Rgb8 {
  uint8_t r, g, b;
};

// ---------------------------------------------------------------------------
// Bilinear interpolation (Pixman-style, 4-bit fractional precision)
// ---------------------------------------------------------------------------

/// Fetch an RGB pixel with EXTEND_PAD: out-of-bounds coordinates clamp to
/// the nearest border pixel, preventing seams at fractional tile edges.
inline Rgb8 FetchRgbPad(const uint8_t* buf, int w, int h, int x, int y) {
  x = std::clamp(x, 0, w - 1);
  y = std::clamp(y, 0, h - 1);
  const size_t off = (static_cast<size_t>(y) * w + x) * 3;
  return {buf[off], buf[off + 1], buf[off + 2]};
}

/// Bilinear-sample an RGB8 source at fractional coordinate (fx, fy).
/// Uses EXTEND_PAD semantics and Pixman's 4-bit fractional precision.
inline Rgb8 BilinearSampleRgb(const uint8_t* src, int src_w, int src_h,
                              double fx, double fy) {
  const int ix = static_cast<int>(std::floor(fx));
  const int iy = static_cast<int>(std::floor(fy));
  const int distx = static_cast<int>((fx - ix) * 16.0 + 0.5);
  const int disty = static_cast<int>((fy - iy) * 16.0 + 0.5);

  if (distx == 0 && disty == 0) {
    return FetchRgbPad(src, src_w, src_h, ix, iy);
  }

  const auto tl = FetchRgbPad(src, src_w, src_h, ix, iy);
  const auto tr = FetchRgbPad(src, src_w, src_h, ix + 1, iy);
  const auto bl = FetchRgbPad(src, src_w, src_h, ix, iy + 1);
  const auto br = FetchRgbPad(src, src_w, src_h, ix + 1, iy + 1);

  const int distxy = distx * disty;
  const int distxiy = distx * (16 - disty);
  const int distixy = disty * (16 - distx);
  const int distixiy = (16 - distx) * (16 - disty);

  auto interp = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t d) -> uint8_t {
    return static_cast<uint8_t>(
        (a * distixiy + b * distxiy + c * distixy + d * distxy + 128) >> 8);
  };

  return {interp(tl.r, tr.r, bl.r, br.r), interp(tl.g, tr.g, bl.g, br.g),
          interp(tl.b, tr.b, bl.b, br.b)};
}

// ---------------------------------------------------------------------------
// Gain correction in linear RGB space
// ---------------------------------------------------------------------------

/// Build a 256-entry lookup table that fuses sRGB→linear→gain→sRGB into a
/// single table lookup per channel value.  The LUT is recomputed only when
/// the gain value changes.
inline void BuildGainLut(float gain, uint8_t lut[256]) {
  for (int v = 0; v < 256; ++v) {
    lut[v] = LinearToSrgb(SrgbToLinear(static_cast<uint8_t>(v)) * gain);
  }
}

/// Apply gain correction to RGB8 data in linear space.
/// Uses a per-gain LUT so the hot loop is a single table lookup per byte --
/// no pow() calls.  If gain ~= 1.0 this is a no-op copy.
inline void ApplyGainLinear(const uint8_t* src, uint8_t* dst,
                            size_t pixel_count, float gain) {
  if (std::abs(gain - 1.0F) < 1e-4F) {
    if (src != dst)
      std::memcpy(dst, src, pixel_count * 3);
    return;
  }

  // Thread-local LUT, rebuilt only when gain changes.
  thread_local float cached_gain = 0.0F;
  thread_local uint8_t lut[256];
  if (cached_gain != gain) {
    BuildGainLut(gain, lut);
    cached_gain = gain;
  }

  const size_t byte_count = pixel_count * 3;
  for (size_t i = 0; i < byte_count; ++i) {
    dst[i] = lut[src[i]];
  }
}

}  // namespace fastslide::pixel

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_PIXEL_OPS_H_
