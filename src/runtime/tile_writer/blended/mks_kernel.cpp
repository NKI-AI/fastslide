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

#include "fastslide/runtime/tile_writer/blended/mks_kernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace fastslide::runtime {

namespace {

constexpr int kMksLutRes = 2000;
constexpr double kMksSupport = 4.5;

constexpr int MksLutSize() {
  return static_cast<int>(kMksSupport * kMksLutRes) + 2;
}

inline double MagicKernelSharp2021(double x) {
  if (x < 0.0)
    x = -x;
  if (x <= 0.5)
    return (577.0 / 576.0) - (239.0 / 144.0) * x * x;
  if (x <= 1.5)
    return (1.0 / 144.0) * (140.0 * x * x - 379.0 * x + 239.0);
  if (x <= 2.5)
    return -(1.0 / 144.0) * (24.0 * x * x - 113.0 * x + 130.0);
  if (x <= 3.5)
    return (1.0 / 144.0) * (4.0 * x * x - 27.0 * x + 45.0);
  if (x <= 4.5) {
    const double t = 2.0 * x - 9.0;
    return -(1.0 / 1152.0) * t * t;
  }
  return 0.0;
}

inline const std::vector<float>& MksLut() {
  static std::vector<float> lut = [] {
    std::vector<float> v(MksLutSize());
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
      v[i] = static_cast<float>(
          MagicKernelSharp2021(static_cast<double>(i) / kMksLutRes));
    }
    return v;
  }();
  return lut;
}

}  // namespace

std::array<float, 2 * kMksRadius + 1> BuildKernel(double frac) {
  std::array<float, 2 * kMksRadius + 1> kernel{};
  const auto& lut = MksLut();

  for (int t = -kMksRadius; t <= kMksRadius; ++t) {
    const double dist = std::abs(static_cast<double>(t) - frac);
    const int idx = std::min(static_cast<int>(std::llround(dist * kMksLutRes)),
                             static_cast<int>(lut.size()) - 1);
    kernel[t + kMksRadius] = lut[idx];
  }

  return kernel;
}

}  // namespace fastslide::runtime
