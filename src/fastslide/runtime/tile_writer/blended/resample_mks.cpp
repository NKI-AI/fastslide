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

#include "fastslide/runtime/tile_writer/blended/resample_mks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "fastslide/runtime/tile_writer/blended/mks_kernel.h"

// Highway SIMD implementation for convolution
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE \
  "src/fastslide/runtime/tile_writer/blended/resample_mks.cpp"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();

namespace fastslide {
namespace runtime {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void ResampleTileSubpixel(const float* src_linear_planar, int w, int h,
                          double frac_x, double frac_y,
                          float* dst_linear_planar) {
  const size_t plane = static_cast<size_t>(w) * h;

  // Skip if fractional offsets are negligible
  constexpr double kEps = 1e-12;
  if (std::abs(frac_x) < kEps && std::abs(frac_y) < kEps) {
    if (src_linear_planar != dst_linear_planar) {
      std::copy(src_linear_planar, src_linear_planar + plane * 3,
                dst_linear_planar);
    }
    return;
  }

  // Build convolution kernels
  const auto kernel_x = BuildKernel(frac_x);
  const auto kernel_y = BuildKernel(frac_y);

  // Temporary buffer for horizontal pass
  std::vector<float> temp(plane * 3);

  const hn::ScalableTag<float> d;
  constexpr int kKernelSize = 2 * kMksRadius + 1;

  // === Horizontal pass ===
  for (int c = 0; c < 3; ++c) {
    const float* src_plane = src_linear_planar + c * plane;
    float* temp_plane = temp.data() + c * plane;

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        // Vectorized convolution over kernel
        auto sum_vec = hn::Zero(d);
        const size_t N = hn::Lanes(d);

        int k = 0;
        // Process kernel in SIMD chunks
        for (; k + N <= kKernelSize; k += N) {
          // Gather source pixels (with reflection)
          alignas(64) float src_vals[16];
          for (size_t i = 0; i < N; ++i) {
            const int src_x = ReflectIndex(x + (k + i) - kMksRadius, w);
            src_vals[i] = src_plane[y * w + src_x];
          }

          const auto src_vec = hn::LoadU(d, src_vals);
          const auto kernel_vec = hn::LoadU(d, &kernel_x[k]);
          sum_vec = hn::MulAdd(src_vec, kernel_vec, sum_vec);
        }

        // Reduce SIMD lanes to scalar
        float sum = hn::ReduceSum(d, sum_vec);

        // Scalar tail for remaining kernel elements
        for (; k < kKernelSize; ++k) {
          const int src_x = ReflectIndex(x + k - kMksRadius, w);
          sum += src_plane[y * w + src_x] * kernel_x[k];
        }

        temp_plane[y * w + x] = sum;
      }
    }
  }

  // === Vertical pass ===
  for (int c = 0; c < 3; ++c) {
    const float* temp_plane = temp.data() + c * plane;
    float* dst_plane = dst_linear_planar + c * plane;

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        // Vectorized convolution over kernel
        auto sum_vec = hn::Zero(d);
        const size_t N = hn::Lanes(d);

        int k = 0;
        // Process kernel in SIMD chunks
        for (; k + N <= kKernelSize; k += N) {
          // Gather source pixels (with reflection)
          alignas(64) float src_vals[16];
          for (size_t i = 0; i < N; ++i) {
            const int src_y = ReflectIndex(y + (k + i) - kMksRadius, h);
            src_vals[i] = temp_plane[src_y * w + x];
          }

          const auto src_vec = hn::LoadU(d, src_vals);
          const auto kernel_vec = hn::LoadU(d, &kernel_y[k]);
          sum_vec = hn::MulAdd(src_vec, kernel_vec, sum_vec);
        }

        // Reduce SIMD lanes to scalar
        float sum = hn::ReduceSum(d, sum_vec);

        // Scalar tail for remaining kernel elements
        for (; k < kKernelSize; ++k) {
          const int src_y = ReflectIndex(y + k - kMksRadius, h);
          sum += temp_plane[src_y * w + x] * kernel_y[k];
        }

        dst_plane[y * w + x] = sum;
      }
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

HWY_EXPORT(ResampleTileSubpixel);

void ResampleTileSubpixel(const float* src_linear_planar, int w, int h,
                          double frac_x, double frac_y,
                          float* dst_linear_planar) {
  HWY_DYNAMIC_DISPATCH(ResampleTileSubpixel)
  (src_linear_planar, w, h, frac_x, frac_y, dst_linear_planar);
}

}  // namespace runtime
}  // namespace fastslide
#endif  // HWY_ONCE
