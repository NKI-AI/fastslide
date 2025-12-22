//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice,
//  this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE

#include "readers/isyntax/third_party/color.h"

#include <algorithm>
#include <cstring>

#include "readers/isyntax/third_party/decompress.h"
#include "readers/isyntax/third_party/platform/intrinsics.h"
#include "readers/isyntax/third_party/utils/mathutils.h"

// Toolchains differ on whether they define `__ARM_NEON` or `__ARM_NEON__`.
// Zig/Clang typically defines `__ARM_NEON` (no trailing underscores).
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace isyntax {
namespace color {

void SignedMagnitudeToAbsoluteValue16Block(int16_t *data, uint32_t len) {
  uint32_t aligned_len = (len / 8) * 8;
  uint32_t i = 0;

#if defined(__SSE2__)
  // Fast x86 SIMD version
  for (; i < aligned_len; i += 8) {
    __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i));
    __m128i sign_masks = _mm_srai_epi16(x, 15);
    __m128i maybe_positive = _mm_andnot_si128(sign_masks, x);
    __m128i value_if_negative =
        _mm_sub_epi16(_mm_and_si128(x, _mm_set1_epi16(0x8000)), x);
    __m128i maybe_negative = _mm_and_si128(sign_masks, value_if_negative);
    __m128i result = _mm_or_si128(maybe_positive, maybe_negative);
    result = _mm_and_si128(result, _mm_set1_epi16(0x7FFF)); // Clear sign bit
    _mm_storeu_si128(reinterpret_cast<__m128i *>(data + i), result);
  }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
  // NEON version for ARM processors
  for (; i < aligned_len; i += 8) {
    uint16x8_t x = vld1q_u16(reinterpret_cast<uint16_t *>(data + i));
    int16x8_t sign_masks = vshrq_n_s16(reinterpret_cast<int16x8_t>(x), 15);
    uint16x8_t maybe_positive =
        vbicq_u16(x, reinterpret_cast<uint16x8_t>(sign_masks));
    uint16x8_t value_if_negative =
        vsubq_u16(vandq_u16(x, vdupq_n_u16(0x8000)), x);
    uint16x8_t maybe_negative =
        vandq_u16(reinterpret_cast<uint16x8_t>(sign_masks), value_if_negative);
    uint16x8_t result = vorrq_u16(maybe_positive, maybe_negative);
    result = vbicq_u16(result, vdupq_n_u16(0x8000)); // Clear sign bit
    vst1q_u16(reinterpret_cast<uint16_t *>(data + i), result);
  }
#endif

  // Scalar version for remaining elements
  for (; i < len; ++i) {
    data[i] = static_cast<int16_t>(
        SignedMagnitudeToTwosComplement16(static_cast<uint16_t>(data[i])) &
        0x7FFF);
  }
}

uint32_t WaveletCoefficientToColorValue(int16_t coefficient) {
#if (DWT_COEFF_BITS == 16)
  uint32_t magnitude =
      static_cast<uint32_t>(SignedMagnitudeToTwosComplement16(coefficient)) &
      ~0x8000;
  return magnitude;
#else
  uint32_t magnitude =
      static_cast<uint32_t>(TwosComplementToSignedMagnitude(coefficient)) &
      ~0x80000000;
  return magnitude;
#endif
}

rgba_t YCoCgToRgb(int16_t Y, int16_t Co, int16_t Cg) {
  int16_t tmp = Y - Cg / 2;
  int16_t G = tmp + Cg;
  int16_t B = tmp - Co / 2;
  int16_t R = B + Co;
  rgba_t result;
  result.r = ATMOST(255, R);
  result.g = ATMOST(255, G);
  result.b = ATMOST(255, B);
  result.a = 255;
  return result;
}

rgba_t YCoCgToBgr(int16_t Y, int16_t Co, int16_t Cg) {
  int16_t tmp = Y - Cg / 2;
  int16_t G = tmp + Cg;
  int16_t B = tmp - Co / 2;
  int16_t R = B + Co;
  rgba_t result;
  result.r = ATMOST(255, B); // Note: BGR order
  result.g = ATMOST(255, G);
  result.b = ATMOST(255, R);
  result.a = 255;
  return result;
}

void ConvertYCoCgToBgraBlock(int16_t *Y, int16_t *Co, int16_t *Cg,
                             int32_t width, int32_t height, int32_t stride,
                             uint32_t *out_bgra) {
  int32_t aligned_width = (width / 8) * 8;

  for (int32_t y = 0; y < height; ++y) {
    uint32_t *dest = out_bgra + (y * width);
    int32_t i = 0;

#if defined(__SSE2__) && defined(__SSSE3__)
    // Fast SIMD version (~2x faster)
    for (; i < aligned_width; i += 8) {
      // Color space conversion
      __m128i Y_ = _mm_loadu_si128(reinterpret_cast<const __m128i *>(Y + i));
      __m128i Co_ = _mm_loadu_si128(reinterpret_cast<const __m128i *>(Co + i));
      __m128i Cg_ = _mm_loadu_si128(reinterpret_cast<const __m128i *>(Cg + i));
      __m128i tmp = _mm_sub_epi16(Y_, _mm_srai_epi16(Cg_, 1)); // tmp = Y - Cg/2
      __m128i G = _mm_add_epi16(tmp, Cg_);                     // G = tmp + Cg
      __m128i B = _mm_sub_epi16(tmp, _mm_srai_epi16(Co_, 1));  // B = tmp - Co/2
      __m128i R = _mm_add_epi16(B, Co_);                       // R = B + Co

      // Clamp to 0..255
      __m128i zero = _mm_set1_epi16(0);
      R = _mm_packus_epi16(R, zero); // -R-R-R-R -> RRRR----
      G = _mm_packus_epi16(zero, G); // -G-G-G-G -> ----GGGG
      B = _mm_packus_epi16(B, zero); // -B-B-B-B -> BBBB----

      __m128i A = _mm_setr_epi32(0, 0, 0xffffffff, 0xffffffff); // ----AAAA

      // Shuffle into BGRA order
      __m128i BG = _mm_or_si128(B, G);
      __m128i RA = _mm_or_si128(R, A);

      __m128i v_perm =
          _mm_setr_epi8(0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15);
      BG = _mm_shuffle_epi8(BG, v_perm);       // BGBGBGBG
      RA = _mm_shuffle_epi8(RA, v_perm);       // RARARARA
      __m128i lo = _mm_unpacklo_epi16(BG, RA); // BGRA
      __m128i hi = _mm_unpackhi_epi16(BG, RA);

      _mm_storeu_si128(reinterpret_cast<__m128i *>(dest + i), lo);
      _mm_storeu_si128(reinterpret_cast<__m128i *>(dest + i + 4), hi);
    }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    // Fast SIMD version for ARM NEON
    for (; i < aligned_width; i += 8) {
      int16x8_t Y_ = vld1q_s16(Y + i);
      int16x8_t Co_ = vld1q_s16(Co + i);
      int16x8_t Cg_ = vld1q_s16(Cg + i);
      int16x8_t tmp = vsubq_s16(Y_, vshrq_n_s16(Cg_, 1));
      int16x8_t G = vaddq_s16(tmp, Cg_);
      int16x8_t B = vsubq_s16(tmp, vshrq_n_s16(Co_, 1));
      int16x8_t R = vaddq_s16(B, Co_);

      uint8x8x4_t bgra_vec;
      bgra_vec.val[2] = vqmovun_s16(R);
      bgra_vec.val[1] = vqmovun_s16(G);
      bgra_vec.val[0] = vqmovun_s16(B);
      bgra_vec.val[3] = vdup_n_u8(0xFF);

      vst4_u8(reinterpret_cast<uint8_t *>(dest + i), bgra_vec);
    }
#endif

    // Scalar version for remaining elements
    for (; i < width; ++i) {
      reinterpret_cast<rgba_t *>(dest)[i] = YCoCgToBgr(Y[i], Co[i], Cg[i]);
    }

    Y += stride;
    Co += stride;
    Cg += stride;
  }
}

void ConvertYCoCgToRgbaBlock(int16_t *Y, int16_t *Co, int16_t *Cg,
                             int32_t width, int32_t height, int32_t stride,
                             uint32_t *out_rgba) {
  int32_t aligned_width = (width / 8) * 8;

  for (int32_t y = 0; y < height; ++y) {
    uint32_t *dest = out_rgba + (y * width);
    int32_t i = 0;

#if defined(__SSE2__) && defined(__SSSE3__)
    // Fast SIMD version (~2x faster)
    for (; i < aligned_width; i += 8) {
      // Color space conversion
      __m128i Y_ = _mm_loadu_si128(reinterpret_cast<const __m128i *>(Y + i));
      __m128i Co_ = _mm_loadu_si128(reinterpret_cast<const __m128i *>(Co + i));
      __m128i Cg_ = _mm_loadu_si128(reinterpret_cast<const __m128i *>(Cg + i));
      __m128i tmp = _mm_sub_epi16(Y_, _mm_srai_epi16(Cg_, 1));
      __m128i G = _mm_add_epi16(tmp, Cg_);
      __m128i B = _mm_sub_epi16(tmp, _mm_srai_epi16(Co_, 1));
      __m128i R = _mm_add_epi16(B, Co_);

      // Clamp to 0..255
      __m128i zero = _mm_set1_epi16(0);
      R = _mm_packus_epi16(R, zero);
      G = _mm_packus_epi16(zero, G);
      B = _mm_packus_epi16(B, zero);

      __m128i A = _mm_setr_epi32(0, 0, 0xffffffff, 0xffffffff);

      // Shuffle into RGBA order
      __m128i RG = _mm_or_si128(R, G);
      __m128i BA = _mm_or_si128(B, A);

      __m128i v_perm =
          _mm_setr_epi8(0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15);
      RG = _mm_shuffle_epi8(RG, v_perm);       // RGRGRGRG
      BA = _mm_shuffle_epi8(BA, v_perm);       // BABABABA
      __m128i lo = _mm_unpacklo_epi16(RG, BA); // RGBA
      __m128i hi = _mm_unpackhi_epi16(RG, BA);

      _mm_storeu_si128(reinterpret_cast<__m128i *>(dest + i), lo);
      _mm_storeu_si128(reinterpret_cast<__m128i *>(dest + i + 4), hi);
    }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    // Fast SIMD version for ARM NEON
    for (; i < aligned_width; i += 8) {
      int16x8_t Y_ = vld1q_s16(Y + i);
      int16x8_t Co_ = vld1q_s16(Co + i);
      int16x8_t Cg_ = vld1q_s16(Cg + i);
      int16x8_t tmp = vsubq_s16(Y_, vshrq_n_s16(Cg_, 1));
      int16x8_t G = vaddq_s16(tmp, Cg_);
      int16x8_t B = vsubq_s16(tmp, vshrq_n_s16(Co_, 1));
      int16x8_t R = vaddq_s16(B, Co_);

      uint8x8x4_t rgba_vec;
      rgba_vec.val[0] = vqmovun_s16(R);
      rgba_vec.val[1] = vqmovun_s16(G);
      rgba_vec.val[2] = vqmovun_s16(B);
      rgba_vec.val[3] = vdup_n_u8(0xFF);

      vst4_u8(reinterpret_cast<uint8_t *>(dest + i), rgba_vec);
    }
#endif

    // Scalar version for remaining elements
    for (; i < width; ++i) {
      reinterpret_cast<rgba_t *>(dest)[i] = YCoCgToRgb(Y[i], Co[i], Cg[i]);
    }

    Y += stride;
    Co += stride;
    Cg += stride;
  }
}

} // namespace color
} // namespace isyntax
