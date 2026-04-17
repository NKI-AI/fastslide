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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_AVERAGE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_AVERAGE_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "fastslide/image.h"
#include "fastslide/resample/utilities.h"

namespace fastslide::resample {

namespace detail {

// Choose an accumulation type that is safe and fast.
template <typename T>
using AccumType = std::conditional_t<
    std::is_floating_point_v<T>, double,
    std::conditional_t<std::is_signed_v<T>, int64_t, uint64_t>>;

// Accumulate one input row into vertical_sum for planar-separate layout.
template <typename T>
inline void AccumulateRowSeparate(const T* row_ptr, uint32_t out_w,
                                  const uint32_t* x_start,
                                  const uint32_t* x_cnt,
                                  typename detail::AccumType<T>* vertical_sum) {
  using A = AccumType<T>;
  for (uint32_t ox = 0; ox < out_w; ++ox) {
    const uint32_t xs = x_start[ox];
    const uint32_t xc = x_cnt[ox];
    A s = 0;
    for (uint32_t i = 0; i < xc; ++i) {
      s += static_cast<A>(row_ptr[xs + i]);
    }
    vertical_sum[ox] += s;
  }
}

// Accumulate one input row into vertical_sum for interleaved layout.
template <typename T>
inline void AccumulateRowContiguous(
    const T* row_ptr, uint32_t out_w, uint32_t stride, const uint32_t* x_start,
    const uint32_t* x_cnt, typename detail::AccumType<T>* vertical_sum) {
  using A = AccumType<T>;
  for (uint32_t ox = 0; ox < out_w; ++ox) {
    const uint32_t xs = x_start[ox];
    const uint32_t xc = x_cnt[ox];
    A s = 0;
    const size_t base = static_cast<size_t>(xs) * stride;
    for (uint32_t i = 0; i < xc; ++i) {
      s += static_cast<A>(row_ptr[base + static_cast<size_t>(i) * stride]);
    }
    vertical_sum[ox] += s;
  }
}

// Fast path for factor==2, planar-separate.
template <typename T, typename IntAccum>
inline void ProcessFactor2Separate(const T* in_data, T* out_data, uint32_t in_w,
                                   uint32_t in_h, uint32_t out_w,
                                   uint32_t out_h, uint32_t channel_index,
                                   uint32_t channels) {
  const size_t in_ch_off = static_cast<size_t>(channel_index) * in_w * in_h;
  const size_t out_ch_off = static_cast<size_t>(channel_index) * out_w * out_h;

  for (uint32_t oy = 0; oy < out_h; ++oy) {
    const uint32_t y0 = oy * 2U;
    const uint32_t y_cnt = std::min(2U, in_h - y0);
    const T* r0 = &in_data[in_ch_off + static_cast<size_t>(y0) * in_w];
    const T* r1 = (y_cnt == 2)
                      ? &in_data[in_ch_off + static_cast<size_t>(y0 + 1) * in_w]
                      : r0;

    const size_t out_row_off = out_ch_off + static_cast<size_t>(oy) * out_w;

    const uint32_t full_out_w = in_w / 2U;
    uint32_t ox = 0;

    for (; ox < full_out_w; ++ox) {
      const uint32_t x0 = ox * 2U;
      const IntAccum s = static_cast<IntAccum>(r0[x0 + 0]) +
                         static_cast<IntAccum>(r0[x0 + 1]) +
                         static_cast<IntAccum>(r1[x0 + 0]) +
                         static_cast<IntAccum>(r1[x0 + 1]);
      out_data[out_row_off + ox] = ClampValue<T>(static_cast<double>(s) * 0.25);
    }

    // Tail column if in_w is odd
    if (full_out_w < out_w) {
      const uint32_t x0 = full_out_w * 2U;
      const uint32_t x_cnt = std::min(2U, in_w - x0);
      IntAccum s = static_cast<IntAccum>(r0[x0]);
      if (x_cnt == 2)
        s += static_cast<IntAccum>(r0[x0 + 1]);
      if (y_cnt == 2) {
        s += static_cast<IntAccum>(r1[x0]);
        if (x_cnt == 2)
          s += static_cast<IntAccum>(r1[x0 + 1]);
      }
      const uint32_t count = x_cnt * y_cnt;
      const double inv = (count == 4U) ? 0.25 : (count == 2U ? 0.5 : 1.0);
      out_data[out_row_off + full_out_w] =
          ClampValue<T>(static_cast<double>(s) * inv);
    }
  }
}

// Fast path for factor==2, interleaved.
template <typename T, typename IntAccum>
inline void ProcessFactor2Contiguous(const T* in_data, T* out_data,
                                     uint32_t in_w, uint32_t in_h,
                                     uint32_t out_w, uint32_t out_h,
                                     uint32_t channel_index,
                                     uint32_t channels) {
  for (uint32_t oy = 0; oy < out_h; ++oy) {
    const uint32_t y0 = oy * 2U;
    const uint32_t y_cnt = std::min(2U, in_h - y0);

    const size_t base0 =
        ((static_cast<size_t>(y0) * in_w) * channels) + channel_index;
    const size_t base1 =
        (y_cnt == 2U) ? (((static_cast<size_t>(y0 + 1U) * in_w) * channels) +
                         channel_index)
                      : base0;

    const T* r0 = &in_data[base0];
    const T* r1 = &in_data[base1];

    const size_t out_row_off =
        ((static_cast<size_t>(oy) * out_w) * channels) + channel_index;

    const uint32_t full_out_w = in_w / 2U;

    for (uint32_t ox = 0; ox < full_out_w; ++ox) {
      const size_t xi = static_cast<size_t>(ox) * 2U * channels;
      const size_t i00 = xi + 0;
      const size_t i01 = xi + channels;
      const IntAccum s =
          static_cast<IntAccum>(r0[i00]) + static_cast<IntAccum>(r0[i01]) +
          static_cast<IntAccum>(r1[i00]) + static_cast<IntAccum>(r1[i01]);
      out_data[out_row_off + static_cast<size_t>(ox) * channels] =
          ClampValue<T>(static_cast<double>(s) * 0.25);
    }

    // Tail column if in_w is odd
    if (full_out_w < out_w) {
      const size_t xi = static_cast<size_t>(full_out_w) * 2U * channels;
      const uint32_t x_cnt =
          std::min(2U, in_w - static_cast<uint32_t>(full_out_w * 2U));
      const size_t i00 = xi + 0;
      const size_t i01 = xi + channels;
      IntAccum s = static_cast<IntAccum>(r0[i00]);
      if (x_cnt == 2)
        s += static_cast<IntAccum>(r0[i01]);
      if (y_cnt == 2) {
        s += static_cast<IntAccum>(r1[i00]);
        if (x_cnt == 2)
          s += static_cast<IntAccum>(r1[i01]);
      }
      const uint32_t count = x_cnt * y_cnt;
      const double inv = (count == 4U) ? 0.25 : (count == 2U ? 0.5 : 1.0);
      out_data[out_row_off + static_cast<size_t>(full_out_w) * channels] =
          ClampValue<T>(static_cast<double>(s) * inv);
    }
  }
}

}  // namespace detail

/// @brief Average‐downsample each channel by `factor` (power of two),
/// averaging all pixels in each block [x*factor .. (x+1)*factor)×[y*factor .. (y+1)*factor),
/// clamped at the image borders (partial tiles handled).
template <ArithmeticType T>
std::unique_ptr<Image> AverageResample(const Image& input, uint32_t factor) {
  if (factor == 0 || (factor & (factor - 1)) != 0) {
    throw std::invalid_argument("factor must be a power of two and >0");
  }

  const uint32_t in_w = input.GetWidth();
  const uint32_t in_h = input.GetHeight();
  const uint32_t chs = input.GetChannels();

  const uint32_t out_w = (in_w + factor - 1) / factor;  // ceil
  const uint32_t out_h = (in_h + factor - 1) / factor;

  const PlanarConfig planar = input.GetPlanarConfig();

  auto output = std::make_unique<Image>(ImageDimensions{out_w, out_h}, chs,
                                        input.GetDataType(), planar);

  const T* __restrict in_data = input.GetDataAs<T>();
  T* __restrict out_data = output->GetDataAs<T>();

  // Precompute x block starts and counts once.
  std::vector<uint32_t> x_start(out_w);
  std::vector<uint32_t> x_cnt(out_w);
  for (uint32_t ox = 0; ox < out_w; ++ox) {
    const uint32_t xs = ox * factor;
    const uint32_t xe = std::min(xs + factor, in_w);
    x_start[ox] = xs;
    x_cnt[ox] = xe - xs;  // 1..factor
  }

  using IntAccum = std::conditional_t<
      (sizeof(T) <= 2), uint32_t,
      std::conditional_t<std::is_unsigned_v<T>, uint64_t, int64_t>>;
  using A = detail::AccumType<T>;

  if (planar == PlanarConfig::kSeparate) {
    for (uint32_t ch = 0; ch < chs; ++ch) {
      // Fast path for factor==2
      if (factor == 2) {
        detail::ProcessFactor2Separate<T, IntAccum>(
            in_data, out_data, in_w, in_h, out_w, out_h, ch, chs);
        continue;
      }

      const size_t in_off = static_cast<size_t>(ch) * in_w * in_h;
      const size_t out_off = static_cast<size_t>(ch) * out_w * out_h;

      std::vector<A> vertical_sum(out_w);

      for (uint32_t oy = 0; oy < out_h; ++oy) {
        const uint32_t ys = oy * factor;
        const uint32_t ye = std::min(ys + factor, in_h);
        const uint32_t ycnt = ye - ys;  // 1..factor

        std::fill(vertical_sum.begin(), vertical_sum.end(), A{0});

        for (uint32_t iy = ys; iy < ye; ++iy) {
          const T* row_ptr = &in_data[in_off + static_cast<size_t>(iy) * in_w];
          detail::AccumulateRowSeparate<T>(row_ptr, out_w, x_start.data(),
                                           x_cnt.data(), vertical_sum.data());
        }

        // Write averaged results.
        for (uint32_t ox = 0; ox < out_w; ++ox) {
          const uint32_t count = x_cnt[ox] * ycnt;
          const double avg =
              (count > 0) ? (static_cast<double>(vertical_sum[ox]) / count)
                          : 0.0;
          const size_t out_idx = out_off + static_cast<size_t>(oy) * out_w + ox;
          out_data[out_idx] = ClampValue<T>(avg);
        }
      }
    }
  } else {  // PlanarConfig::kContiguous
    for (uint32_t ch = 0; ch < chs; ++ch) {
      // Fast path for factor==2
      if (factor == 2) {
        detail::ProcessFactor2Contiguous<T, IntAccum>(
            in_data, out_data, in_w, in_h, out_w, out_h, ch, chs);
        continue;
      }

      std::vector<A> vertical_sum(out_w);

      for (uint32_t oy = 0; oy < out_h; ++oy) {
        const uint32_t ys = oy * factor;
        const uint32_t ye = std::min(ys + factor, in_h);
        const uint32_t ycnt = ye - ys;

        std::fill(vertical_sum.begin(), vertical_sum.end(), A{0});

        for (uint32_t iy = ys; iy < ye; ++iy) {
          const size_t base = ((static_cast<size_t>(iy) * in_w) * chs) + ch;
          const T* row_ptr = &in_data[base];
          detail::AccumulateRowContiguous<T>(row_ptr, out_w, chs,
                                             x_start.data(), x_cnt.data(),
                                             vertical_sum.data());
        }

        for (uint32_t ox = 0; ox < out_w; ++ox) {
          const uint32_t count = x_cnt[ox] * ycnt;
          const double avg =
              (count > 0) ? (static_cast<double>(vertical_sum[ox]) / count)
                          : 0.0;
          const size_t out_idx =
              (((static_cast<size_t>(oy) * out_w) + ox) * chs) + ch;
          out_data[out_idx] = ClampValue<T>(avg);
        }
      }
    }
  }

  return output;
}

/// @brief Generic dispatcher so caller can pick factor at runtime.
inline std::unique_ptr<Image> AverageResample(const Image& input,
                                              uint32_t factor) {
  std::unique_ptr<Image> result;
  DispatchByDataType(input.GetDataType(), [&]<typename T>() {
    result = AverageResample<T>(input, factor);
  });
  return result;
}

/// @brief Convenience helpers
inline std::unique_ptr<Image> Average2x2Resample(const Image& input) {
  return AverageResample(input, 2);
}

inline std::unique_ptr<Image> Average4x4Resample(const Image& input) {
  return AverageResample(input, 4);
}

inline std::unique_ptr<Image> Average8x8Resample(const Image& input) {
  return AverageResample(input, 8);
}

}  // namespace fastslide::resample

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_AVERAGE_H_
