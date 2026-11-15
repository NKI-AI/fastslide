// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLOR_LUT_HWY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLOR_LUT_HWY_H_

#include <cstddef>
#include <cstdint>

namespace fastslide {

/// @brief Apply a precomputed 256^3 RGB->RGB byte LUT in place using Highway.
///
/// The LUT holds one 3-byte entry per RGB triple, indexed by
/// `((R * 256) + G) * 256 + B`. Entry `e` occupies `lut[3e], lut[3e+1],
/// lut[3e+2]` = output `(R, G, B)`. The SIMD gather path reads a full 32-bit
/// word at byte offset `idx * 3` and masks off the high byte, so the LUT buffer
/// MUST be allocated with one extra padding byte (`256^3 * 3 + 1`) to keep the
/// read of the final entry in bounds.
///
/// @param lut          LUT bytes (`256^3 * 3 + 1` in size; see above).
/// @param data         Interleaved 8-bit pixel buffer, transformed in place.
/// @param pixel_count  Number of pixels in @p data.
/// @param channels     Channels per pixel: 3 (RGB) or 4 (RGBA). For RGBA the
///                     alpha byte is preserved. Any other value is a no-op.
void ApplyRgbLutInterleavedHwy(const uint8_t* lut, uint8_t* data,
                               size_t pixel_count, uint32_t channels);

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLOR_LUT_HWY_H_
