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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_SRGB_LINEAR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_SRGB_LINEAR_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "fastslide/runtime/tile_writer/fs_profile.h"

namespace fastslide::runtime {

/// @brief Convert sRGB8 interleaved tile to linear RGB planar (float)
/// @param src_interleaved Input sRGB8 buffer (interleaved RGB, sized w * h * 3)
/// @param w Tile width
/// @param h Tile height
/// @param dst_linear_planar Output linear RGB buffer (planar format, must be
///                          sized w * h * 3 + kSimdPadding to prevent SIMD
///                          buffer overruns)
void ConvertSrgb8ToLinearPlanar(const uint8_t* src_interleaved, int w, int h,
                                float* dst_linear_planar);

/// @brief Apply gain correction to linear RGB planar data
/// @param linear_planar Planar float array (R plane, G plane, B plane).
///                      Must be sized plane_size * 3 + kSimdPadding to prevent
///                      SIMD buffer overruns
/// @param plane_size Size of each plane (not including padding)
/// @param gain Gain factor to multiply
void GainCorrectionLinearPlanar(float* linear_planar, size_t plane_size,
                                float gain);

/// @brief Convert linear RGB value to sRGB [0,1]
double LinearToSrgb01(double v);

// Lookup table for Linear→sRGB conversion (4096 entries for 12-bit precision)
constexpr int kLinearToSrgbLutSize = 4096;
extern const std::array<uint8_t, kLinearToSrgbLutSize + 1> kLinearToSrgb8Lut;

/// @brief Convert linear RGB value [0,1] to sRGB8 using fast LUT
/// Inline definition for zero function call overhead in hot loops
inline uint8_t LinearToSrgb8Fast(float v) {
  // Fast path: clamp to [0, 1]
  if (v <= 0.0f)
    return 0;
  if (v >= 1.0f)
    return 255;

  // Direct LUT access - no function call overhead
  // Using float arithmetic is faster than double on most architectures
  const int idx = static_cast<int>(v * kLinearToSrgbLutSize);
  return kLinearToSrgb8Lut[idx];
}

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_SRGB_LINEAR_H_
