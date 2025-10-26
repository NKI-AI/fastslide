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

#include "fastslide/runtime/tile_writer/blended/accumulate.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/runtime/tile_writer/blended/srgb_linear.h"

// Highway SIMD implementation for FinalizeLinearToSrgb8
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE \
  "src/fastslide/runtime/tile_writer/blended/accumulate.cpp"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();

namespace fastslide {
namespace runtime {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

/// @brief Fast reciprocal approximation using Newton-Raphson refinement
///
/// Computes 1/w faster than division using:
/// 1. Hardware approximation (ApproximateReciprocal)
/// 2. One Newton-Raphson iteration: r = r * (2 - w*r)
/// This gives ~23 bits of precision (float32) at 3-4x speedup vs division.
/// Zero weights return zero (prevents division by zero).
///
/// @param d Highway descriptor for vector type
/// @param w Input vector (weights)
/// @return Approximate reciprocal 1/w
template <class D>
HWY_INLINE hn::Vec<D> ReciprocalFast(D d, hn::Vec<D> w) {
  const auto two = hn::Set(d, 2.0f);
  auto r = hn::ApproximateReciprocal(w);        // r0
  r = hn::Mul(r, hn::Sub(two, hn::Mul(w, r)));  // one NR iteration
  // Zero-protect:
  const auto zero = hn::Zero(d);
  const auto nz = hn::Gt(w, zero);
  return hn::IfThenElse(nz, r, zero);
}

/// @brief Float copy of the uint8 LUT so we can vector-gather directly
inline const float* LinearToSrgb8LutF32() {
  static float kLutF32[kLinearToSrgbLutSize + 1];
  static std::atomic<bool> init{false};
  if (!init.load(std::memory_order_acquire)) {
    for (int i = 0; i <= kLinearToSrgbLutSize; ++i) {
      kLutF32[i] = static_cast<float>(kLinearToSrgb8Lut[i]);  // 0..255 as float
    }
    init.store(true, std::memory_order_release);
  }
  return kLutF32;
}

/// @brief SIMD linear-to-sRGB conversion via LUT with vectorized gather
///
/// Converts linear RGB values [0,1] to sRGB values [0,255] using a
/// pre-computed lookup table. Uses SIMD gather operations for parallel
/// LUT access across vector lanes.
///
/// Process:
/// 1. Clamp input to [0,1]
/// 2. Scale to LUT index range [0, kLinearToSrgbLutSize]
/// 3. Convert to int32 indices
/// 4. Gather from float LUT (returns 0..255 as float)
///
/// @param d Highway descriptor for vector type
/// @param v Input linear RGB values [0,1]
/// @return sRGB values [0,255] as float (caller converts to uint8)
template <class D>
HWY_INLINE hn::Vec<D> LinearToSrgb8VecF32(D d, hn::Vec<D> v) {
  const auto zero = hn::Zero(d);
  const auto one = hn::Set(d, 1.0f);

  v = hn::Clamp(v, zero, one);
  const auto scaled =
      hn::Mul(v, hn::Set(d, static_cast<float>(kLinearToSrgbLutSize)));

  using I32 = hn::Rebind<int32_t, D>;
  const auto idx = hn::Min(hn::ConvertTo(I32(), scaled),
                           hn::Set(I32(), kLinearToSrgbLutSize));

  const float* lut = LinearToSrgb8LutF32();
  return hn::GatherIndex(d, lut, idx);  // lanes are float 0..255
}

// Highway-optimized version with cache-blocking for planar input
void FinalizeLinearToSrgb8(const float* accumulator_r,
                           const float* accumulator_g,
                           const float* accumulator_b, const float* weight_sum,
                           int img_w, int img_h, uint8_t* out_interleaved) {

  const hn::ScalableTag<float> df;
  const hn::Rebind<uint8_t, decltype(df)> du8;
  const hn::Rebind<int32_t, decltype(df)> di32;

  // Cache-blocking: process in 64x64 tiles for L1 cache efficiency
  // Each tile: 64×64×4 bytes × 4 arrays (R,G,B,W) ≈ 64KB (fits in L1)
  constexpr int kTileSize = 64;

  const size_t VL = hn::Lanes(df);

  // Calculate grid dimensions
  const int num_tiles_y = (img_h + kTileSize - 1) / kTileSize;
  const int num_tiles_x = (img_w + kTileSize - 1) / kTileSize;
  const int total_tiles = num_tiles_y * num_tiles_x;

  // Get thread pool for parallel tile processing
  auto& pool = aifocore::ThreadPoolManager::GetInstance();

  // Process tiles in parallel (no mutex needed - non-overlapping writes)
  auto futures = pool.submit_sequence(0, total_tiles, [&](size_t tile_idx) {
    const int tile_y_idx = static_cast<int>(tile_idx) / num_tiles_x;
    const int tile_x_idx = static_cast<int>(tile_idx) % num_tiles_x;

    const int tile_y = tile_y_idx * kTileSize;
    const int tile_x = tile_x_idx * kTileSize;

    const int tile_h = std::min(kTileSize, img_h - tile_y);
    const int tile_w = std::min(kTileSize, img_w - tile_x);

    // Process pixels within tile
    for (int ty = 0; ty < tile_h; ++ty) {
      const int y = tile_y + ty;
      const size_t row_base = y * img_w + tile_x;
      const int pixels_in_row = tile_w;
      const size_t full = (pixels_in_row / static_cast<int>(VL)) * VL;

      // Vectorized main loop within tile row
      for (size_t i = 0; i < full; i += VL) {
        const size_t pix_idx = row_base + i;

        // Load planar RGB and weights (sequential, cache-friendly)
        const auto r = hn::LoadU(df, accumulator_r + pix_idx);
        const auto g = hn::LoadU(df, accumulator_g + pix_idx);
        const auto b = hn::LoadU(df, accumulator_b + pix_idx);
        const auto w = hn::LoadU(df, weight_sum + pix_idx);

        // Fast reciprocal for normalization
        const auto inv_w = ReciprocalFast(df, w);

        // Normalize: divide by weight
        const auto r_norm = hn::Mul(r, inv_w);
        const auto g_norm = hn::Mul(g, inv_w);
        const auto b_norm = hn::Mul(b, inv_w);

        // Linear→sRGB via LUT (returns float 0..255)
        const auto r_f = LinearToSrgb8VecF32(df, r_norm);
        const auto g_f = LinearToSrgb8VecF32(df, g_norm);
        const auto b_f = LinearToSrgb8VecF32(df, b_norm);

        // Convert float→int32→uint8
        const auto r_i = hn::ConvertTo(di32, r_f);
        const auto g_i = hn::ConvertTo(di32, g_f);
        const auto b_i = hn::ConvertTo(di32, b_f);

        const auto r8 = hn::DemoteTo(du8, r_i);
        const auto g8 = hn::DemoteTo(du8, g_i);
        const auto b8 = hn::DemoteTo(du8, b_i);

        // Store interleaved RGB8
        hn::StoreInterleaved3(r8, g8, b8, du8, out_interleaved + pix_idx * 3);
      }

      // Scalar tail for remaining pixels in row
      for (size_t i = full; i < static_cast<size_t>(pixels_in_row); ++i) {
        const size_t pix_idx = row_base + i;

        const float w = weight_sum[pix_idx];
        const float inv_w = (w > 0.0f) ? (1.0f / w) : 0.0f;

        // Normalize accumulated linear RGB
        const float r_linear = accumulator_r[pix_idx] * inv_w;
        const float g_linear = accumulator_g[pix_idx] * inv_w;
        const float b_linear = accumulator_b[pix_idx] * inv_w;

        // Convert linear to sRGB using scalar function
        out_interleaved[pix_idx * 3 + 0] = LinearToSrgb8Fast(r_linear);
        out_interleaved[pix_idx * 3 + 1] = LinearToSrgb8Fast(g_linear);
        out_interleaved[pix_idx * 3 + 2] = LinearToSrgb8Fast(b_linear);
      }
    }
  });

  // Wait for all tiles to complete
  futures.wait();
}

// Highway-optimized tile accumulation with planar output
void AccumulateLinearTile(const float* linear_planar, int w, int h, int base_x,
                          int base_y, float weight, float* accumulator_r,
                          float* accumulator_g, float* accumulator_b,
                          float* weight_sum, int img_w, int img_h,
                          absl::Mutex& accumulator_mutex) {
  // Clip intersection once - no per-pixel bounds checks!
  const int x0 = std::max(0, -base_x);
  const int y0 = std::max(0, -base_y);
  const int x1 = std::min(w, img_w - base_x);
  const int y1 = std::min(h, img_h - base_y);

  if (x0 >= x1 || y0 >= y1)
    return;  // No intersection

  const size_t plane = static_cast<size_t>(w) * h;
  const int span_width = x1 - x0;

  // Debug: verify dimensions are valid
  DCHECK_GT(img_w, 0);
  DCHECK_GT(img_h, 0);
  DCHECK_GE(base_x + x0, 0);
  DCHECK_GE(base_y + y0, 0);
  DCHECK_LE(base_x + x1, img_w);
  DCHECK_LE(base_y + y1, img_h);

  // Hoist per-channel plane pointers from source
  const float* srcR = linear_planar + 0 * plane;
  const float* srcG = linear_planar + 1 * plane;
  const float* srcB = linear_planar + 2 * plane;

  const hn::ScalableTag<float> df;
  const size_t N = hn::Lanes(df);
  const auto weight_vec = hn::Set(df, weight);

  // Branch-free row-by-row accumulation with mutex protection
  // Lock once per tile instead of per-pixel to minimize overhead
  absl::MutexLock lock(&accumulator_mutex);

  for (int ty = y0; ty < y1; ++ty) {
    const size_t src_base = ty * w + x0;
    const size_t dst_base = (base_y + ty) * img_w + (base_x + x0);

    int i = 0;
    const int full = (span_width / static_cast<int>(N)) * static_cast<int>(N);

    // Vectorized main loop with planar accumulation
    for (; i < full; i += N) {
      const size_t src_idx = src_base + i;
      const size_t dst_idx = dst_base + i;

      // Load planar RGB source values
      const auto r_src = hn::LoadU(df, srcR + src_idx);
      const auto g_src = hn::LoadU(df, srcG + src_idx);
      const auto b_src = hn::LoadU(df, srcB + src_idx);

      // Load planar accumulator values (sequential access, cache-friendly)
      const auto r_acc = hn::LoadU(df, accumulator_r + dst_idx);
      const auto g_acc = hn::LoadU(df, accumulator_g + dst_idx);
      const auto b_acc = hn::LoadU(df, accumulator_b + dst_idx);

      // Accumulate with FMA: acc = acc + src * weight
      const auto r_new = hn::MulAdd(r_src, weight_vec, r_acc);
      const auto g_new = hn::MulAdd(g_src, weight_vec, g_acc);
      const auto b_new = hn::MulAdd(b_src, weight_vec, b_acc);

      // Store planar accumulator values (sequential access, cache-friendly)
      hn::StoreU(r_new, df, accumulator_r + dst_idx);
      hn::StoreU(g_new, df, accumulator_g + dst_idx);
      hn::StoreU(b_new, df, accumulator_b + dst_idx);

      // Accumulate weight sum
      const auto w_sum = hn::LoadU(df, weight_sum + dst_idx);
      const auto w_sum_new = hn::Add(w_sum, weight_vec);
      hn::StoreU(w_sum_new, df, weight_sum + dst_idx);
    }

    // Scalar tail for remaining pixels in the row
    for (; i < span_width; ++i) {
      const size_t src_idx = src_base + i;
      const size_t dst_idx = dst_base + i;

      accumulator_r[dst_idx] += srcR[src_idx] * weight;
      accumulator_g[dst_idx] += srcG[src_idx] * weight;
      accumulator_b[dst_idx] += srcB[src_idx] * weight;
      weight_sum[dst_idx] += weight;
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

HWY_EXPORT(AccumulateLinearTile);
HWY_EXPORT(FinalizeLinearToSrgb8);

void AccumulateLinearTile(const float* linear_planar, int w, int h, int base_x,
                          int base_y, float weight, float* accumulator_r,
                          float* accumulator_g, float* accumulator_b,
                          float* weight_sum, int img_w, int img_h,
                          absl::Mutex& accumulator_mutex) {
  HWY_DYNAMIC_DISPATCH(AccumulateLinearTile)
  (linear_planar, w, h, base_x, base_y, weight, accumulator_r, accumulator_g,
   accumulator_b, weight_sum, img_w, img_h, accumulator_mutex);
}

void FinalizeLinearToSrgb8(const float* accumulator_r,
                           const float* accumulator_g,
                           const float* accumulator_b, const float* weight_sum,
                           int img_w, int img_h, uint8_t* out_interleaved) {
  HWY_DYNAMIC_DISPATCH(FinalizeLinearToSrgb8)
  (accumulator_r, accumulator_g, accumulator_b, weight_sum, img_w, img_h,
   out_interleaved);
}

}  // namespace runtime
}  // namespace fastslide
#endif  // HWY_ONCE
