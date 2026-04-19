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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_MKS_KERNEL_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_MKS_KERNEL_H_

#include <array>

namespace fastslide::runtime {

constexpr int kMksRadius = 5;

/// @brief Build 1D Magic Kernel for fractional shift
std::array<float, 2 * kMksRadius + 1> BuildKernel(double frac);

/// @brief Reflect index at boundaries (symmetric extension)
/// Optimized for common case where index is slightly out of bounds
inline int ReflectIndex(int idx, int size) {
  if (size <= 0) {
    return 0;
  }
  if (size == 1) {
    return 0;
  }

  // Common case: idx is in bounds
  if (idx >= 0 && idx < size) {
    return idx;
  }

  const int period = 2 * (size - 1);

  // Fast reflection for slightly negative indices
  if (idx < 0 && idx >= -size) {
    return -idx;
  }

  // Fast reflection for slightly over-bounds indices
  if (idx >= size && idx < 2 * size) {
    return period - idx;
  }

  // Rare case: need full reflection with modulo
  int reflected_idx = (idx < 0) ? -idx : (idx - size);
  reflected_idx = reflected_idx % period;

  if (reflected_idx >= size) {
    reflected_idx = period - reflected_idx;
  }

  return reflected_idx;
}

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_MKS_KERNEL_H_
