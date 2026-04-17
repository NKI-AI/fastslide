//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE

#pragma once

#include <cstdint>
#include <span>

#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/utils/mathutils.h"

namespace isyntax {
namespace color {

/// @brief Convert two's complement to signed magnitude representation (32-bit)
///
/// This function is its own inverse.
/// @param x Value in two's complement format
/// @return Value in signed magnitude format
constexpr int32_t TwosComplementToSignedMagnitude(uint32_t x) {
  uint32_t m = -(x >> 31);
  int32_t result = (~m & x) | (((x & 0x80000000) - x) & m);
  return result;
}

/// @brief Convert block of signed magnitude to absolute values (16-bit)
///
/// Converts signed magnitude representation to absolute values, clearing the
/// sign bit. Uses SIMD optimizations when available (SSE2 or ARM NEON).
///
/// @param data Buffer to convert in-place
/// @param len Number of 16-bit elements
void SignedMagnitudeToAbsoluteValue16Block(int16_t* data, uint32_t len);

/// @brief Get magnitude of wavelet coefficient as color value
///
/// Extracts the magnitude (absolute value) from a wavelet coefficient.
/// @param coefficient Wavelet coefficient
/// @return Magnitude as color value (0-255 range expected)
uint32_t WaveletCoefficientToColorValue(int16_t coefficient);

/// @brief Convert single pixel from YCoCg to RGB color space
///
/// YCoCg is a reversible color transform used in iSyntax:
/// - Y: Luma
/// - Co: Chroma orange-cyan
/// - Cg: Chroma green-magenta
///
/// @param Y Luma component
/// @param Co Chroma orange component
/// @param Cg Chroma green component
/// @return RGBA pixel (alpha set to 255)
rgba_t YCoCgToRgb(int16_t Y, int16_t Co, int16_t Cg);

/// @brief Convert single pixel from YCoCg to BGR color space
///
/// Same as YCoCgToRgb but with BGR channel ordering.
///
/// @param Y Luma component
/// @param Co Chroma orange component
/// @param Cg Chroma green component
/// @return BGRA pixel (alpha set to 255)
rgba_t YCoCgToBgr(int16_t Y, int16_t Co, int16_t Cg);

/// @brief Convert block of pixels from YCoCg to BGRA with SIMD optimization
///
/// Converts a rectangular block of YCoCg coefficients to BGRA pixels.
/// Uses SIMD instructions (SSE2/SSSE3 or ARM NEON) when available for
/// ~2x speedup.
///
/// @param Y Luma channel (size: stride * height)
/// @param Co Chroma orange channel
/// @param Cg Chroma green channel
/// @param width Block width in pixels
/// @param height Block height in pixels
/// @param stride Row stride (may be > width for padding)
/// @param out_bgra Output buffer (size: width * height pixels)
void ConvertYCoCgToBgraBlock(int16_t* Y, int16_t* Co, int16_t* Cg,
                             int32_t width, int32_t height, int32_t stride,
                             uint32_t* out_bgra);

/// @brief Convert block of pixels from YCoCg to RGBA with SIMD optimization
///
/// Same as ConvertYCoCgToBgraBlock but with RGBA channel ordering.
///
/// @param Y Luma channel
/// @param Co Chroma orange channel
/// @param Cg Chroma green channel
/// @param width Block width in pixels
/// @param height Block height in pixels
/// @param stride Row stride
/// @param out_rgba Output buffer (size: width * height pixels)
void ConvertYCoCgToRgbaBlock(int16_t* Y, int16_t* Co, int16_t* Cg,
                             int32_t width, int32_t height, int32_t stride,
                             uint32_t* out_rgba);

}  // namespace color
}  // namespace isyntax
