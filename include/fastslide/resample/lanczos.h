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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_LANCZOS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_LANCZOS_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdlib>
#include <memory>
#include <new>
#include <numbers>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "fastslide/image.h"
#include "fastslide/resample/utilities.h"

#ifdef __AVX2__
#include <immintrin.h>
#endif

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#define HAVE_NEON 1
#else
#define HAVE_NEON 0
#endif

namespace fastslide::resample {

//---------------------------------------------------------------------------------------
// Aligned memory allocation for SIMD efficiency
//---------------------------------------------------------------------------------------

/// @brief Custom deleter for 64-byte aligned memory allocation
struct AlignedDeleter {
  void operator()(void* ptr) const noexcept { std::free(ptr); }
};

/// @brief Allocate 64-byte aligned memory for SIMD operations
/// @param size Size in bytes to allocate
/// @return Unique pointer to aligned memory
inline std::unique_ptr<float, AlignedDeleter> AllocateAligned(size_t size) {
  void* ptr = std::aligned_alloc(64, ((size * sizeof(float) + 63) / 64) * 64);
  if (!ptr) {
    throw std::bad_alloc();
  }
  return std::unique_ptr<float, AlignedDeleter>(static_cast<float*>(ptr));
}

//---------------------------------------------------------------------------------------
// Window selection and kernel configuration
//---------------------------------------------------------------------------------------

enum class WindowType { kLanczos, kCosine };

/// @brief Mathematical tolerance for weight normalization
constexpr double kNormalizationTolerance = 1e-8;

template <int A>
concept ValidKernelSize = (A >= 1 && A <= 5);

constexpr double Sinc(double x) noexcept {
  if (x == 0.0)
    return 1.0;
  double pi_x = std::numbers::pi * x;
  return std::sin(pi_x) / pi_x;
}

template <int A>
requires ValidKernelSize<A>

constexpr double LanczosKernel(double x) noexcept {
  if (std::abs(x) >= A)
    return 0.0;
  return Sinc(x) * Sinc(x / A);
}

template <int A>
requires ValidKernelSize<A>

constexpr double CosineWindowedSinc(double x) noexcept {
  if (std::abs(x) >= A)
    return 0.0;
  // sinc(x)
  double s =
      (x == 0.0 ? 1.0
                : std::sin(std::numbers::pi * x) / (std::numbers::pi * x));
  // raised-cosine taper
  double w = 0.5 * (1.0 + std::cos(std::numbers::pi * x / A));
  return s * w;
}

template <int A>
requires ValidKernelSize<A>

constexpr double ComputeWeight(double x, WindowType window_type) noexcept {
  switch (window_type) {
    case WindowType::kLanczos:
      return LanczosKernel<A>(x);
    case WindowType::kCosine:
      return CosineWindowedSinc<A>(x);
    default:
      return LanczosKernel<A>(x);
  }
}

constexpr int ReflectIndex(int x, int max) noexcept {
  if (x < 0)
    return -x - 1;
  if (x >= max)
    return 2 * max - x - 1;
  return x;
}

//---------------------------------------------------------------------------------------
// Precomputed kernel cache for performance
//---------------------------------------------------------------------------------------

/// @brief Runtime-initialized kernel lookup table
template <int A, WindowType WT>
requires ValidKernelSize<A>
static const auto KernelTable = []() {
  constexpr int M = 1024;
  std::array<float, M + 1> t{};
  for (int i = 0; i <= M; ++i) {
    float x = i * (static_cast<float>(A) / M);
    t[i] = static_cast<float>(ComputeWeight<A>(x, WT));
  }
  return t;
}();

//---------------------------------------------------------------------------------------
// Kernel configuration structs for compile-time optimization
//---------------------------------------------------------------------------------------

template <int A, WindowType WT>
requires ValidKernelSize<A>

struct KernelConfig {
  static constexpr int kKernelSize = A;
  static constexpr WindowType kWindowType = WT;
  static constexpr int kKernelSupport = 2 * A + 1;

  static constexpr float ComputeKernelWeight(double x) noexcept {
    x = std::abs(x);
    if (x >= A)
      return 0.0f;

    constexpr int M = 1024;
    constexpr float scale = M / static_cast<float>(A);
    float scaled = static_cast<float>(x) * scale;  // in [0…M]
    int idx = static_cast<int>(scaled);
    if (idx >= M)
      return KernelTable<A, WT>[M];

    float frac = scaled - idx;
    // linear interpolation
    float w0 = KernelTable<A, WT>[idx];
    float w1 = KernelTable<A, WT>[idx + 1];
    return w0 + (w1 - w0) * frac;
  }
};

using Lanczos2 = KernelConfig<2, WindowType::kLanczos>;
using Lanczos3 = KernelConfig<3, WindowType::kLanczos>;
using Cosine2 = KernelConfig<2, WindowType::kCosine>;
using Cosine3 = KernelConfig<3, WindowType::kCosine>;

//---------------------------------------------------------------------------------------
// Buffer allocation using std::vector for simplicity and performance
//---------------------------------------------------------------------------------------

/// @brief Combined index and weight for improved cache locality
struct Tap {
  int idx;
  float w;
};

/// @brief Precomputed scaling parameters for weight computation
template <typename KernelT>
struct WeightParams {
  double scale;
  double fscale;
  double inv_fscale;
  int radius;
  int support;
};

/// @brief Prepare weight computation parameters (loop-invariant calculations)
/// @tparam KernelT Kernel configuration type
/// @param input_dim Input dimension (width or height)
/// @param output_dim Output dimension (width or height)
/// @return Precomputed parameters for weight computation
template <typename KernelT>
constexpr WeightParams<KernelT> PrepareWeights(uint32_t input_dim,
                                               uint32_t output_dim) noexcept {
  double scale = static_cast<double>(input_dim) / output_dim;
  double fscale = std::max(1.0, scale);
  double inv_fscale = 1.0 / fscale;
  int radius = static_cast<int>(std::ceil(KernelT::kKernelSize * fscale));
  int support = (radius * 2) + 1;

  return WeightParams<KernelT>{scale, fscale, inv_fscale, radius, support};
}

template <typename KernelT>
int ComputeSupport(uint32_t input_dim, uint32_t output_dim) noexcept {
  auto params = PrepareWeights<KernelT>(input_dim, output_dim);
  return params.support;
}

template <typename KernelT>
int ComputeSampleWeights(uint32_t length,  // output dimension (width or height)
                         uint32_t input_dim,  // input dimension (in_w or in_h)
                         Tap* out_taps  // pointer into thread‐local pool
                         ) noexcept {
  // Precompute all loop-invariant parameters
  auto params = PrepareWeights<KernelT>(input_dim, length);

  for (uint32_t i = 0; i < length; ++i, out_taps += params.support) {
    double s = ((i + 0.5) * params.scale) - 0.5;
    int base = static_cast<int>(std::floor(s));
    float sum = 0.0F;

    for (int k = -params.radius; k <= params.radius; ++k) {
      int tap = k + params.radius;
      out_taps[tap].idx = ReflectIndex(base + k, input_dim);
      float w = KernelT::ComputeKernelWeight(
          ((s - (base + k) + 0.5) * params.inv_fscale));
      out_taps[tap].w = w;
      sum += w;
    }

    bool valid_sum =
        std::abs(sum) > static_cast<float>(kNormalizationTolerance);
    float inv = valid_sum ? (1.0f / sum) : 0.0f;
    int center = params.support / 2;

    for (int t = 0; t < params.support; ++t) {
      out_taps[t].w =
          valid_sum ? (out_taps[t].w * inv) : (t == center ? 1.0f : 0.0f);
    }
  }
  return params.support;
}

template <ArithmeticType T, typename KernelT>
std::unique_ptr<Image> ResampleSeparable(const Image& input,
                                         uint32_t output_width,
                                         uint32_t output_height) {

  uint32_t in_w = input.GetWidth();
  uint32_t in_h = input.GetHeight();
  uint32_t chs = input.GetChannels();

  auto output =
      std::make_unique<Image>(ImageDimensions{output_width, output_height}, chs,
                              input.GetDataType(), PlanarConfig::kSeparate);

  // Get direct pointers to input and output data
  const T* __restrict input_data = input.GetDataAs<T>();
  T* __restrict output_data = output->GetDataAs<T>();

  // Use thread-local persistent storage to avoid repeated allocations
  thread_local static std::vector<Tap> taps_x;
  thread_local static std::vector<Tap> taps_y;
  thread_local static std::unique_ptr<float, AlignedDeleter>
      intermediate_buffer;
  thread_local static size_t intermediate_buffer_size = 0;

  // Compute support values needed for buffer allocation
  int support_x = ComputeSupport<KernelT>(in_w, output_width);
  int support_y = ComputeSupport<KernelT>(in_h, output_height);

  // Align intermediate buffer stride to multiples of 8 for SIMD efficiency
  uint32_t intermediate_stride = ((output_width + 7) / 8) * 8;

  // Reserve space in vectors to avoid reallocation during reuse
  taps_x.reserve(static_cast<size_t>(output_width) * support_x);
  taps_y.reserve(static_cast<size_t>(output_height) * support_y);

  // Calculate required buffer size and reallocate if needed
  size_t required_size = static_cast<size_t>(intermediate_stride) * in_h * chs;
  if (!intermediate_buffer || intermediate_buffer_size < required_size) {
    intermediate_buffer = AllocateAligned(required_size);
    intermediate_buffer_size = required_size;
  }

  // Resize vectors to exact sizes needed
  taps_x.resize(static_cast<size_t>(output_width) * support_x);
  taps_y.resize(static_cast<size_t>(output_height) * support_y);

  // Get aligned pointers for performance - intermediate buffer is guaranteed
  // 64-byte aligned
  Tap* __restrict taps_x_ptr = taps_x.data();
  Tap* __restrict taps_y_ptr = taps_y.data();
  float* __restrict intermediate = intermediate_buffer.get();

  // Compute sample weights
  ComputeSampleWeights<KernelT>(output_width, in_w, taps_x_ptr);
  ComputeSampleWeights<KernelT>(output_height, in_h, taps_y_ptr);

  // 2) horizontal pass
  HorizontalPass<T>(input_data, in_w, in_h, chs, output_width, support_x,
                    taps_x_ptr, intermediate, intermediate_stride);

  // 3) vertical pass
  if constexpr (std::is_same_v<T, uint16_t>) {
#if defined(__AVX2__)
    VerticalPassAVX2<uint16_t>(intermediate, in_h, output_width, output_height,
                               chs, support_y, taps_y_ptr, output_data,
                               intermediate_stride);
#else
    VerticalPass<uint16_t>(intermediate, in_h, output_width, output_height, chs,
                           support_y, taps_y_ptr, output_data,
                           intermediate_stride);
#endif
  } else {
    // everything else (float, uint8_t, etc.) uses the generic
    VerticalPass<T>(intermediate, in_h, output_width, output_height, chs,
                    support_y, taps_y_ptr, output_data, intermediate_stride);
  }

  return output;
}

/// @brief Horizontal resampling pass with SIMD-friendly stride support
/// @tparam T Input data type (uint8_t, uint16_t, float, etc.)
/// @param input_data Input image data in planar format
/// @param in_w Input image width
/// @param in_h Input image height
/// @param chs Number of image channels
/// @param output_width Target output width (actual data width, not padded)
/// @param support_x Horizontal kernel support size
/// @param taps_x_ptr Precomputed horizontal resampling taps
/// @param intermediate Output buffer with SIMD-friendly stride
/// @param intermediate_stride SIMD-padded stride for intermediate buffer rows
///
/// This function performs horizontal resampling using a SIMD-friendly
/// intermediate buffer. The intermediate buffer uses padded rows (stride >
/// output_width) to enable efficient vectorized processing in subsequent
/// operations. Each row is padded to multiples of 8 to facilitate SIMD
/// optimization.
///
/// Future SIMD optimization: The inner loop can be vectorized as:
/// for(int x = 0; x < output_width; x += 8) {
///   // Load 8 samples at once and process in parallel
///   // ... vectorized kernel application
/// }
template <typename T>
[[gnu::hot]] void HorizontalPass(const T* __restrict input_data, uint32_t in_w,
                                 uint32_t in_h, uint32_t chs,
                                 uint32_t output_width, int support_x,
                                 const Tap* __restrict taps_x_ptr,
                                 float* __restrict intermediate,
                                 uint32_t intermediate_stride) noexcept {

  constexpr size_t kRowBlockSize =
      4;  // Process 4 rows at a time for better cache utilization

  for (uint32_t y_block = 0; y_block < in_h; y_block += kRowBlockSize) {
    uint32_t rows_in_block =
        std::min(static_cast<uint32_t>(kRowBlockSize), in_h - y_block);

    // Process multiple rows together for better cache utilization
    for (uint32_t y_offset = 0; y_offset < rows_in_block; ++y_offset) {
      uint32_t y = y_block + y_offset;

      for (uint32_t c = 0; c < chs; ++c) {
        // Hoist channel-based pointer calculations outside inner loops
        const size_t channel_input_offset =
            c * static_cast<size_t>(in_w) * in_h;
        const size_t channel_intermediate_offset =
            c * in_h * intermediate_stride;

        // Hoist pointer calculation for current row and channel (planar layout)
        const T* __restrict row_base =
            input_data + (channel_input_offset + static_cast<size_t>(y) * in_w);

        // Use SIMD-friendly stride for intermediate buffer indexing
        float* __restrict intermediate_row =
            intermediate +
            (channel_intermediate_offset + y * intermediate_stride);

        // Process each output pixel in this row
        for (uint32_t x = 0; x < output_width; ++x) {
          float pixel_sum = 0.0f;

          // Extract taps for this pixel
          const Tap* __restrict pixel_taps = taps_x_ptr + x * support_x;

          // Optimized inner loop - vectorizer-friendly with constant trip count
#ifdef __clang__
#pragma clang loop vectorize(enable) unroll_count(8)
#elif defined(__GNUC__)
#pragma GCC ivdep
#pragma unroll 8
#endif
          for (int k = 0; k < support_x; ++k) {
            pixel_sum += static_cast<float>(row_base[pixel_taps[k].idx]) *
                         pixel_taps[k].w;
          }

          intermediate_row[x] = pixel_sum;
        }

        // Note: Padding region (from output_width to intermediate_stride)
        // remains uninitialized but won't be accessed by subsequent operations
      }
    }
  }
}

/// @brief Vertical resampling pass with strip-mining optimization
// for improved cache locality
/// @tparam T Output data type (uint8_t, uint16_t, float, etc.)
/// @param intermediate Input intermediate buffer with SIMD-friendly stride
/// @param in_h Input height dimension
/// @param output_width Target output width (actual data width, not padded)
/// @param output_height Target output height
/// @param chs Number of image channels
/// @param support_y Vertical kernel support size
/// @param taps_y_ptr Precomputed vertical resampling taps
/// @param output_data Output image data buffer
/// @param intermediate_stride SIMD-padded stride for intermediate buffer rows
///
/// This function performs vertical resampling using strip-mining optimization
/// to improve cache locality. Instead of processing the entire width at once,
/// it divides the width into smaller strips and processes each strip completely
/// before moving to the next.
///
/// Strip-mining benefits:
/// - Keeps intermediate buffer data hot in L2 cache
/// - Reduces cache misses by maintaining temporal locality
/// - Working set per strip: support_y * strip_size * sizeof(float)
/// - Optimal for kernels with large vertical support (e.g., Lanczos3)
///
/// Memory access pattern per strip [xs, xe):
/// 1. For each channel and output row, process pixels xs to xe-1
/// 2. Each pixel accesses support_y rows from intermediate buffer
/// 3. Strip size is tuned so working set fits in L2 cache budget
// Forward declaration for AVX2-optimized vertical pass
template <typename T>
[[gnu::hot]] void VerticalPassAVX2(const float* __restrict intermediate,
                                   uint32_t in_h, uint32_t output_width,
                                   uint32_t output_height, uint32_t chs,
                                   int support_y,
                                   const Tap* __restrict taps_y_ptr,
                                   T* __restrict output_data,
                                   uint32_t intermediate_stride) noexcept;

template <typename T>
[[gnu::hot]] void VerticalPass(const float* __restrict intermediate,
                               uint32_t in_h, uint32_t output_width,
                               uint32_t output_height, uint32_t chs,
                               int support_y, const Tap* __restrict taps_y_ptr,
                               T* __restrict output_data,
                               uint32_t intermediate_stride) noexcept {

  // Use a sensible default strip size for cache-friendly processing
  constexpr uint32_t strip_size = 128;

  // Process width in strips to maintain cache locality
  for (uint32_t x_start = 0; x_start < output_width; x_start += strip_size) {
    const uint32_t x_end = std::min(output_width, x_start + strip_size);

    // Process each strip completely before moving to the next
    // This keeps the intermediate buffer data for this strip hot in L2 cache
    for (uint32_t c = 0; c < chs; ++c) {
      // Hoist channel-based calculations outside inner loops
      const size_t channel_output_offset =
          c * static_cast<size_t>(output_width) * output_height;
      const size_t channel_intermediate_base = c * in_h;

      for (uint32_t y = 0; y < output_height; ++y) {
        T* __restrict out_row =
            output_data +
            (channel_output_offset + static_cast<size_t>(y) * output_width);

        // Extract taps for this output row
        const Tap* __restrict row_taps = taps_y_ptr + y * support_y;

        // Process pixels in current strip [x_start, x_end)
        for (uint32_t x = x_start; x < x_end; ++x) {
          float pixel_sum = 0.0f;

          // Optimized inner loop - vectorizer-friendly with constant trip count
          // This accesses support_y rows from intermediate buffer at column x
#ifdef __clang__
#pragma clang loop vectorize(enable) unroll_count(8)
#elif defined(__GNUC__)
#pragma GCC ivdep
#pragma unroll 8
#endif
          for (int k = 0; k < support_y; ++k) {
            // Use SIMD-friendly stride for intermediate buffer indexing
            pixel_sum +=
                intermediate[(channel_intermediate_base + row_taps[k].idx) *
                                 intermediate_stride +
                             x] *
                row_taps[k].w;
          }

          out_row[x] = ClampValue<T>(static_cast<double>(pixel_sum));
        }
      }
    }
  }
}

// clamp to [0, 65535]
#ifdef __AVX2__
static inline __m256 clamp_0_65535(__m256 v) {
  __m256 zero = _mm256_setzero_ps();
  __m256 hi = _mm256_set1_ps(65535.0f);
  return _mm256_min_ps(_mm256_max_ps(v, zero), hi);
}
#endif

#ifdef __AVX2__
template <>
void VerticalPassAVX2<uint16_t>(const float* __restrict intermediate,
                                uint32_t in_h, uint32_t W, uint32_t H,
                                uint32_t C, int support_y,
                                const Tap* __restrict taps,
                                uint16_t* __restrict output,
                                uint32_t stride) noexcept {
  // precompute a float bias of 32768.0f for signed‐saturating pack dance
  const __m256 bias = _mm256_set1_ps(32768.0f);
  for (uint32_t c = 0; c < C; ++c) {
    // Hoist channel-based calculations outside inner loops
    const size_t channel_output_offset = c * W * H;
    const size_t channel_intermediate_base = c * in_h;

    for (uint32_t y = 0; y < H; ++y) {
      uint16_t* __restrict out_row = output + channel_output_offset + y * W;

      // Extract taps for this output row
      const Tap* __restrict row_taps = taps + y * support_y;

      // process 8 at a time
      for (uint32_t x = 0; x < W; x += 8) {
        // 1) accumulate into a __m256
        __m256 acc = _mm256_setzero_ps();
        for (int k = 0; k < support_y; ++k) {
          const float* row =
              intermediate +
              (channel_intermediate_base + row_taps[k].idx) * stride + x;
          // aligned loads OK because stride is padded to multiple of 8
          __m256 v = _mm256_load_ps(row);
          __m256 w = _mm256_set1_ps(row_taps[k].w);
          acc = _mm256_fmadd_ps(v, w, acc);
        }

        // 2) clamp to [0,65535]
        acc = clamp_0_65535(acc);
        // 3) subtract 32768 → now range [-32768…+32767]
        __m256 adj = _mm256_sub_ps(acc, bias);
        // 4) float→int32
        __m256i i32 = _mm256_cvtps_epi32(adj);

        // 5) extract 128‑bit halves
        __m128i lo = _mm256_extractf128_si256(i32, 0);
        __m128i hi = _mm256_extractf128_si256(i32, 1);
        // 6) signed‑saturating pack 32→16
        __m128i packed = _mm_packs_epi32(lo, hi);
        // 7) add bias back (→ [0…65535])
        packed = _mm_add_epi16(packed, _mm_set1_epi16(32768));

        // 8) store 8×uint16
        // NOLINTNEXTLINE(readability/casting)
        _mm_storeu_si128((__m128i*)(out_row + x), packed);
      }
    }
  }
}
#endif

// Public API functions
inline std::unique_ptr<Image> LanczosResample(const Image& input, uint32_t w,
                                              uint32_t h) {
  if (input.GetPlanarConfig() != PlanarConfig::kSeparate) {
    throw std::invalid_argument(
        "LanczosResample requires separate planar configuration");
  }

  std::unique_ptr<Image> r;
  DispatchByDataType(input.GetDataType(), [&]<typename T>() {
    r = ResampleSeparable<T, Lanczos3>(input, w, h);
  });
  return r;
}

inline std::unique_ptr<Image> Lanczos2Resample(const Image& input, uint32_t w,
                                               uint32_t h) {
  if (input.GetPlanarConfig() != PlanarConfig::kSeparate) {
    throw std::invalid_argument(
        "Lanczos2Resample requires separate planar configuration");
  }

  std::unique_ptr<Image> r;
  DispatchByDataType(input.GetDataType(), [&]<typename T>() {
    r = ResampleSeparable<T, Lanczos2>(input, w, h);
  });
  return r;
}

inline std::unique_ptr<Image> CosineResample(const Image& input, uint32_t w,
                                             uint32_t h) {
  if (input.GetPlanarConfig() != PlanarConfig::kSeparate) {
    throw std::invalid_argument(
        "CosineResample requires separate planar configuration");
  }

  std::unique_ptr<Image> r;
  DispatchByDataType(input.GetDataType(), [&]<typename T>() {
    r = ResampleSeparable<T, Cosine3>(input, w, h);
  });
  return r;
}

}  // namespace fastslide::resample

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_LANCZOS_H_
