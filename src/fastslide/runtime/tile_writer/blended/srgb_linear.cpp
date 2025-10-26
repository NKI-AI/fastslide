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

#include "fastslide/runtime/tile_writer/blended/srgb_linear.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"

// Highway SIMD implementation for ConvertSrgb8ToLinearPlanar
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE \
  "src/fastslide/runtime/tile_writer/blended/srgb_linear.cpp"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();

namespace fastslide {
namespace runtime {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

namespace {

inline double SrgbToLinear01(double s) {
  if (s <= 0.04045)
    return s / 12.92;
  return std::pow((s + 0.055) / 1.055, 2.4);
}

inline const std::array<float, 256>& Srgb8ToLinearLut() {
  static std::array<float, 256> lut = [] {
    std::array<float, 256> v{};
    for (int i = 0; i < 256; ++i) {
      v[i] = static_cast<float>(SrgbToLinear01(static_cast<double>(i) / 255.0));
    }
    return v;
  }();
  return lut;
}

}  // namespace

// Highway-optimized version for portable SIMD
void ConvertSrgb8ToLinearPlanar(const uint8_t* src_interleaved, int w, int h,
                                float* dst_linear_planar) {
  // Debug: verify dimensions and pointers
  DCHECK_GT(w, 0);
  DCHECK_GT(h, 0);
  DCHECK_NE(src_interleaved, nullptr);
  DCHECK_NE(dst_linear_planar, nullptr);

  const auto& lut = Srgb8ToLinearLut();
  const size_t plane = static_cast<size_t>(w) * h;
  const size_t total = plane;

  const hn::ScalableTag<float> d_f32;
  const hn::Rebind<int32_t, decltype(d_f32)> d_i32;
  const hn::Rebind<uint8_t, decltype(d_i32)> d_u8_quarter;  // Quarter size

  const size_t N_f32 = hn::Lanes(d_f32);
  const size_t pixels_per_iter = N_f32;

  float* dstR = dst_linear_planar + 0 * plane;
  float* dstG = dst_linear_planar + 1 * plane;
  float* dstB = dst_linear_planar + 2 * plane;

  // Main vectorized loop
  size_t pix = 0;
  for (; pix + pixels_per_iter <= total; pix += pixels_per_iter) {
    const uint8_t* src_ptr = src_interleaved + pix * 3;

    // Load interleaved RGB uint8 (quarter-size to match int32 count)
    hn::Vec<decltype(d_u8_quarter)> r8, g8, b8;
    hn::LoadInterleaved3(d_u8_quarter, src_ptr, r8, g8, b8);

    // Convert uint8 to int32 for indexing (uint8 -> uint16 -> int32)
    const hn::Rebind<uint16_t, decltype(d_i32)> d_u16_half;
    const auto r_u16 = hn::PromoteTo(d_u16_half, r8);
    const auto g_u16 = hn::PromoteTo(d_u16_half, g8);
    const auto b_u16 = hn::PromoteTo(d_u16_half, b8);

    const auto r_i32 = hn::PromoteTo(d_i32, r_u16);
    const auto g_i32 = hn::PromoteTo(d_i32, g_u16);
    const auto b_i32 = hn::PromoteTo(d_i32, b_u16);

    // Gather from LUT
    const auto r_f32 = hn::GatherIndex(d_f32, lut.data(), r_i32);
    const auto g_f32 = hn::GatherIndex(d_f32, lut.data(), g_i32);
    const auto b_f32 = hn::GatherIndex(d_f32, lut.data(), b_i32);

    // Store to planar layout (use unaligned stores since plane boundaries may
    // not be 64-byte aligned for small tiles)
    hn::StoreU(r_f32, d_f32, dstR + pix);
    hn::StoreU(g_f32, d_f32, dstG + pix);
    hn::StoreU(b_f32, d_f32, dstB + pix);
  }

  // Handle remaining pixels with scalar code to avoid buffer over-read
  // LoadInterleaved3 doesn't support masking, so we use scalar LUT lookups
  for (; pix < total; ++pix) {
    const uint8_t* src_ptr = src_interleaved + pix * 3;
    dstR[pix] = lut[src_ptr[0]];
    dstG[pix] = lut[src_ptr[1]];
    dstB[pix] = lut[src_ptr[2]];
  }
}

void GainCorrectionLinearPlanar(float* linear_planar, size_t plane_size,
                                float gain) {
  // Debug: verify pointer and dimensions
  DCHECK_NE(linear_planar, nullptr);
  DCHECK_GT(plane_size, 0);

  const hn::ScalableTag<float> d;
  const size_t total_size = plane_size * 3;
  const auto gain_vec = hn::Set(d, gain);

  // Process all elements including tail with Highway's masking
  size_t i = 0;
  for (; i + hn::Lanes(d) <= total_size; i += hn::Lanes(d)) {
    auto vec = hn::LoadU(d, linear_planar + i);
    vec = hn::Mul(vec, gain_vec);
    hn::StoreU(vec, d, linear_planar + i);
  }

  // Handle remaining elements with masked load/store
  if (i < total_size) {
    const size_t remaining = total_size - i;
    const auto mask = hn::FirstN(d, remaining);
    auto vec = hn::MaskedLoad(mask, d, linear_planar + i);
    vec = hn::Mul(vec, gain_vec);
    hn::BlendedStore(vec, mask, d, linear_planar + i);
  }
}

}  // namespace HWY_NAMESPACE
}  // namespace runtime
}  // namespace fastslide

HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fastslide {
namespace runtime {

// Lookup table for Linear→sRGB conversion at fastslide::runtime namespace scope
// Definition for the extern declaration in the header
const std::array<uint8_t, kLinearToSrgbLutSize + 1> kLinearToSrgb8Lut = [] {
  std::array<uint8_t, kLinearToSrgbLutSize + 1> v{};
  for (int i = 0; i <= kLinearToSrgbLutSize; ++i) {
    const double linear = static_cast<double>(i) / kLinearToSrgbLutSize;
    double srgb;
    if (linear <= 0.0031308) {
      srgb = 12.92 * linear;
    } else {
      srgb = 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    }
    v[i] =
        static_cast<uint8_t>(std::clamp(std::round(srgb * 255.0), 0.0, 255.0));
  }
  return v;
}();  // /app/src/models

HWY_EXPORT(ConvertSrgb8ToLinearPlanar);
HWY_EXPORT(GainCorrectionLinearPlanar);

void ConvertSrgb8ToLinearPlanar(const uint8_t* src_interleaved, int w, int h,
                                float* dst_linear_planar) {
  HWY_DYNAMIC_DISPATCH(ConvertSrgb8ToLinearPlanar)
  (src_interleaved, w, h, dst_linear_planar);
}

void GainCorrectionLinearPlanar(float* linear_planar, size_t plane_size,
                                float gain) {
  HWY_DYNAMIC_DISPATCH(GainCorrectionLinearPlanar)
  (linear_planar, plane_size, gain);
}

double LinearToSrgb01(double v) {
  if (v <= 0.0)
    return 0.0;
  if (v >= 1.0)
    return 1.0;
  if (v <= 0.0031308)
    return 12.92 * v;
  return 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

// LinearToSrgb8Fast is now defined inline in the header for cross-TU inlining

}  // namespace runtime
}  // namespace fastslide
#endif  // HWY_ONCE
