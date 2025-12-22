#pragma once

// NOLINTBEGIN
// Code from the openjp2 library:
// inverse discrete wavelet transform (5/3)

// See: https://github.com/uclouvain/openjpeg
// The OpenJPEG license information is included below:
/*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
 * Copyright (c) 2017, IntoPIX SA <support@intopix.com>
 * Copyright (c) 2021, Pieter Valkema
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
// End of OpenJPEG copyright notice.

#include "readers/isyntax/third_party/isyntax.h"

// The OpenJPEG DWT implementation below uses SSE2/AVX2 intrinsics when
// available. GCC/Clang only define types like `__m128i`/`__m256i` after
// including the proper intrinsics header.
#if defined(__SSE2__) || defined(__AVX2__)
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif
#endif

#if (DWT_COEFF_BITS == 16)
#ifdef __AVX2__
/** Number of int32 values in a AVX2 register */
#define VREG_INT_COUNT 16
#else
/** Number of int32 values in a SSE2 register */
#define VREG_INT_COUNT 8
#endif
#else
#ifdef __AVX2__
/** Number of int32 values in a AVX2 register */
#define VREG_INT_COUNT 8
#else
/** Number of int32 values in a SSE2 register */
#define VREG_INT_COUNT 4
#endif
#endif

/** Number of columns that we can process in parallel in the vertical pass */
#define PARALLEL_COLS_53 (2 * VREG_INT_COUNT)

typedef struct dwt_local {
  icoeff_t* mem;
  int32_t dn;  /* number of elements in high pass band */
  int32_t sn;  /* number of elements in low pass band */
  int32_t cas; /* 0 = start on even coord, 1 = start on odd coord */
} opj_dwt_t;

static void opj_idwt53_h_cas0(icoeff_t* tmp, const int32_t sn,
                              const int32_t len, icoeff_t* tiledp) {
  int32_t i, j;
  const icoeff_t* in_even = &tiledp[0];
  const icoeff_t* in_odd = &tiledp[sn];

  icoeff_t d1c, d1n, s1n, s0c, s0n;

  ASSERT(len > 1);

  /* Improved version of the TWO_PASS_VERSION: */
  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */
  s1n = in_even[0];
  d1n = in_odd[0];
  s0n = s1n - ((d1n + 1) >> 1);

  for (i = 0, j = 1; i < (len - 3); i += 2, j++) {
    d1c = d1n;
    s0c = s0n;

    s1n = in_even[j];
    d1n = in_odd[j];

    s0n = s1n - ((d1c + d1n + 2) >> 2);

    tmp[i] = s0c;
    tmp[i + 1] = d1c + ((s0c + s0n) >> 1);
  }

  tmp[i] = s0n;

  if (len & 1) {
    tmp[len - 1] = in_even[(len - 1) / 2] - ((d1n + 1) >> 1);
    tmp[len - 2] = d1n + ((s0n + tmp[len - 1]) >> 1);
  } else {
    tmp[len - 1] = d1n + s0n;
  }
  memcpy(tiledp, tmp, (uint32_t)len * sizeof(icoeff_t));
}

static void opj_idwt53_h_cas1(icoeff_t* tmp, const int32_t sn,
                              const int32_t len, icoeff_t* tiledp) {
  int32_t i, j;
  const icoeff_t* in_even = &tiledp[sn];
  const icoeff_t* in_odd = &tiledp[0];

  icoeff_t s1, s2, dc, dn;

  ASSERT(len > 2);

  /* Improved version of the TWO_PASS_VERSION: */
  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */

  s1 = in_even[1];
  dc = in_odd[0] - ((in_even[0] + s1 + 2) >> 2);
  tmp[0] = in_even[0] + dc;

  for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

    s2 = in_even[j + 1];

    dn = in_odd[j] - ((s1 + s2 + 2) >> 2);
    tmp[i] = dc;
    tmp[i + 1] = s1 + ((dn + dc) >> 1);

    dc = dn;
    s1 = s2;
  }

  tmp[i] = dc;

  if (!(len & 1)) {
    dn = in_odd[len / 2 - 1] - ((s1 + 1) >> 1);
    tmp[len - 2] = s1 + ((dn + dc) >> 1);
    tmp[len - 1] = dn;
  } else {
    tmp[len - 1] = s1 + dc;
  }
  memcpy(tiledp, tmp, (uint32_t)len * sizeof(icoeff_t));
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void opj_idwt53_h(const opj_dwt_t* dwt, icoeff_t* tiledp) {
  const int32_t sn = dwt->sn;
  const int32_t len = sn + dwt->dn;
  if (dwt->cas == 0) { /* Left-most sample is on even coordinate */
    if (len > 1) {
      opj_idwt53_h_cas0(dwt->mem, sn, len, tiledp);
    } else {
      /* Unmodified value */
    }
  } else { /* Left-most sample is on odd coordinate */
    if (len == 1) {
      tiledp[0] /= 2;
    } else if (len == 2) {
      icoeff_t* out = dwt->mem;
      const icoeff_t* in_even = &tiledp[sn];
      const icoeff_t* in_odd = &tiledp[0];
      out[1] = in_odd[0] - ((in_even[0] + 1) >> 1);
      out[0] = in_even[0] + out[1];
      memcpy(tiledp, dwt->mem, (uint32_t)len * sizeof(icoeff_t));
    } else if (len > 2) {
      opj_idwt53_h_cas1(dwt->mem, sn, len, tiledp);
    }
  }
}

#if (defined(__SSE2__) || defined(__AVX2__))

/* Conveniency macros to improve the readabilty of the formulas */
#if defined(__AVX2__)
#define VREG __m256i
#if (DWT_COEFF_BITS == 16)
#define LOAD_CST(x) _mm256_set1_epi16(x)
#define ADD(x, y) _mm256_add_epi16((x), (y))
#define SUB(x, y) _mm256_sub_epi16((x), (y))
#define SAR(x, y) _mm256_srai_epi16((x), (y))
#else
#define LOAD_CST(x) _mm256_set1_epi32(x)
#define ADD(x, y) _mm256_add_epi32((x), (y))
#define SUB(x, y) _mm256_sub_epi32((x), (y))
#define SAR(x, y) _mm256_srai_epi32((x), (y))
#endif
#define LOAD(x) _mm256_load_si256((const VREG*)(x))
#define LOADU(x) _mm256_loadu_si256((const VREG*)(x))
#define STORE(x, y) _mm256_store_si256((VREG*)(x), (y))
#define STOREU(x, y) _mm256_storeu_si256((VREG*)(x), (y))
#else
#define VREG __m128i
#if (DWT_COEFF_BITS == 16)
#define LOAD_CST(x) _mm_set1_epi16(x)
#define ADD(x, y) _mm_add_epi16((x), (y))
#define SUB(x, y) _mm_sub_epi16((x), (y))
#define SAR(x, y) _mm_srai_epi16((x), (y))
#else
#define LOAD_CST(x) _mm_set1_epi32(x)
#define ADD(x, y) _mm_add_epi32((x), (y))
#define SUB(x, y) _mm_sub_epi32((x), (y))
#define SAR(x, y) _mm_srai_epi32((x), (y))
#endif
#define LOAD(x) _mm_load_si128((const VREG*)(x))
#define LOADU(x) _mm_loadu_si128((const VREG*)(x))
#define STORE(x, y) _mm_store_si128((VREG*)(x), (y))
#define STOREU(x, y) _mm_storeu_si128((VREG*)(x), (y))
#endif
#define ADD3(x, y, z) ADD(ADD(x, y), z)

static void opj_idwt53_v_final_memcpy(icoeff_t* tiledp_col, const icoeff_t* tmp,
                                      int32_t len, size_t stride) {
  for (int32_t i = 0; i < len; ++i) {
    /* A memcpy(&tiledp_col[i * stride + 0],
                                        &tmp[PARALLEL_COLS_53 * i + 0],
                                        PARALLEL_COLS_53 * sizeof(int32_t))
                   would do but would be a tiny bit slower.
                   We can take here advantage of our knowledge of alignment */
    STOREU(&tiledp_col[(size_t)i * stride + 0],
           LOAD(&tmp[PARALLEL_COLS_53 * i + 0]));
    STOREU(&tiledp_col[(size_t)i * stride + VREG_INT_COUNT],
           LOAD(&tmp[PARALLEL_COLS_53 * i + VREG_INT_COUNT]));
  }
}

/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on even coordinate */
static void opj_idwt53_v_cas0_mcols_SSE2_OR_AVX2(icoeff_t* tmp,
                                                 const int32_t sn,
                                                 const int32_t len,
                                                 icoeff_t* tiledp_col,
                                                 const size_t stride) {
  const icoeff_t* in_even = &tiledp_col[0];
  const icoeff_t* in_odd = &tiledp_col[(size_t)sn * stride];

  int32_t i;
  size_t j;
  VREG d1c_0, d1n_0, s1n_0, s0c_0, s0n_0;
  VREG d1c_1, d1n_1, s1n_1, s0c_1, s0n_1;
  const VREG two = LOAD_CST(2);

  ASSERT(len > 1);
  /*#if __AVX2__
        ASSERT(PARALLEL_COLS_53 == 16);
    ASSERT(VREG_INT_COUNT == 8);
#else
        ASSERT(PARALLEL_COLS_53 == 8);
        ASSERT(VREG_INT_COUNT == 4);
#endif*/

  /* Note: loads of input even/odd values must be done in a unaligned */
  /* fashion. But stores in tmp can be done with aligned store, since */
  /* the temporary buffer is properly aligned */
  ASSERT((size_t)tmp % (sizeof(icoeff_t) * VREG_INT_COUNT) == 0);

  s1n_0 = LOADU(in_even + 0);
  s1n_1 = LOADU(in_even + VREG_INT_COUNT);
  d1n_0 = LOADU(in_odd);
  d1n_1 = LOADU(in_odd + VREG_INT_COUNT);

  /* s0n = s1n - ((d1n + 1) >> 1); <==> */
  /* s0n = s1n - ((d1n + d1n + 2) >> 2); */
  s0n_0 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
  s0n_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));

  for (i = 0, j = 1; i < (len - 3); i += 2, j++) {
    d1c_0 = d1n_0;
    s0c_0 = s0n_0;
    d1c_1 = d1n_1;
    s0c_1 = s0n_1;

    s1n_0 = LOADU(in_even + j * stride);
    s1n_1 = LOADU(in_even + j * stride + VREG_INT_COUNT);
    d1n_0 = LOADU(in_odd + j * stride);
    d1n_1 = LOADU(in_odd + j * stride + VREG_INT_COUNT);

    /*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
    s0n_0 = SUB(s1n_0, SAR(ADD3(d1c_0, d1n_0, two), 2));
    s0n_1 = SUB(s1n_1, SAR(ADD3(d1c_1, d1n_1, two), 2));

    STORE(tmp + PARALLEL_COLS_53 * (i + 0), s0c_0);
    STORE(tmp + PARALLEL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0c_1);

    /* d1c + ((s0c + s0n) >> 1) */
    STORE(tmp + PARALLEL_COLS_53 * (i + 1) + 0,
          ADD(d1c_0, SAR(ADD(s0c_0, s0n_0), 1)));
    STORE(tmp + PARALLEL_COLS_53 * (i + 1) + VREG_INT_COUNT,
          ADD(d1c_1, SAR(ADD(s0c_1, s0n_1), 1)));
  }

  STORE(tmp + PARALLEL_COLS_53 * (i + 0) + 0, s0n_0);
  STORE(tmp + PARALLEL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0n_1);

  if (len & 1) {
    VREG tmp_len_minus_1;
    s1n_0 = LOADU(in_even + (size_t)((len - 1) / 2) * stride);
    /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
    tmp_len_minus_1 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
    STORE(tmp + PARALLEL_COLS_53 * (len - 1), tmp_len_minus_1);
    /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
    STORE(tmp + PARALLEL_COLS_53 * (len - 2),
          ADD(d1n_0, SAR(ADD(s0n_0, tmp_len_minus_1), 1)));

    s1n_1 = LOADU(in_even + (size_t)((len - 1) / 2) * stride + VREG_INT_COUNT);
    /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
    tmp_len_minus_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));
    STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT, tmp_len_minus_1);
    /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
    STORE(tmp + PARALLEL_COLS_53 * (len - 2) + VREG_INT_COUNT,
          ADD(d1n_1, SAR(ADD(s0n_1, tmp_len_minus_1), 1)));

  } else {
    STORE(tmp + PARALLEL_COLS_53 * (len - 1) + 0, ADD(d1n_0, s0n_0));
    STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT,
          ADD(d1n_1, s0n_1));
  }

  opj_idwt53_v_final_memcpy(tiledp_col, tmp, len, stride);
}

/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on odd coordinate */
static void opj_idwt53_v_cas1_mcols_SSE2_OR_AVX2(icoeff_t* tmp,
                                                 const int32_t sn,
                                                 const int32_t len,
                                                 icoeff_t* tiledp_col,
                                                 const size_t stride) {
  int32_t i;
  size_t j;

  VREG s1_0, s2_0, dc_0, dn_0;
  VREG s1_1, s2_1, dc_1, dn_1;
  const VREG two = LOAD_CST(2);

  const icoeff_t* in_even = &tiledp_col[(size_t)sn * stride];
  const icoeff_t* in_odd = &tiledp_col[0];

  ASSERT(len > 2);
  /*#if __AVX2__
        ASSERT(PARALLEL_COLS_53 == 16);
    ASSERT(VREG_INT_COUNT == 8);
#else
        ASSERT(PARALLEL_COLS_53 == 8);
        ASSERT(VREG_INT_COUNT == 4);
#endif*/

  /* Note: loads of input even/odd values must be done in a unaligned */
  /* fashion. But stores in tmp can be done with aligned store, since */
  /* the temporary buffer is properly aligned */
  ASSERT((size_t)tmp % (sizeof(icoeff_t) * VREG_INT_COUNT) == 0);

  s1_0 = LOADU(in_even + stride);
  /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
  dc_0 = SUB(LOADU(in_odd + 0), SAR(ADD3(LOADU(in_even + 0), s1_0, two), 2));
  STORE(tmp + PARALLEL_COLS_53 * 0, ADD(LOADU(in_even + 0), dc_0));

  s1_1 = LOADU(in_even + stride + VREG_INT_COUNT);
  /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
  dc_1 = SUB(LOADU(in_odd + VREG_INT_COUNT),
             SAR(ADD3(LOADU(in_even + VREG_INT_COUNT), s1_1, two), 2));
  STORE(tmp + PARALLEL_COLS_53 * 0 + VREG_INT_COUNT,
        ADD(LOADU(in_even + VREG_INT_COUNT), dc_1));

  for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

    s2_0 = LOADU(in_even + (j + 1) * stride);
    s2_1 = LOADU(in_even + (j + 1) * stride + VREG_INT_COUNT);

    /* dn = in_odd[j * stride] - ((s1 + s2 + 2) >> 2); */
    dn_0 = SUB(LOADU(in_odd + j * stride), SAR(ADD3(s1_0, s2_0, two), 2));
    dn_1 = SUB(LOADU(in_odd + j * stride + VREG_INT_COUNT),
               SAR(ADD3(s1_1, s2_1, two), 2));

    STORE(tmp + PARALLEL_COLS_53 * i, dc_0);
    STORE(tmp + PARALLEL_COLS_53 * i + VREG_INT_COUNT, dc_1);

    /* tmp[i + 1] = s1 + ((dn + dc) >> 1); */
    STORE(tmp + PARALLEL_COLS_53 * (i + 1) + 0,
          ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
    STORE(tmp + PARALLEL_COLS_53 * (i + 1) + VREG_INT_COUNT,
          ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

    dc_0 = dn_0;
    s1_0 = s2_0;
    dc_1 = dn_1;
    s1_1 = s2_1;
  }
  STORE(tmp + PARALLEL_COLS_53 * i, dc_0);
  STORE(tmp + PARALLEL_COLS_53 * i + VREG_INT_COUNT, dc_1);

  if (!(len & 1)) {
    /*dn = in_odd[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
    dn_0 = SUB(LOADU(in_odd + (size_t)(len / 2 - 1) * stride),
               SAR(ADD3(s1_0, s1_0, two), 2));
    dn_1 = SUB(LOADU(in_odd + (size_t)(len / 2 - 1) * stride + VREG_INT_COUNT),
               SAR(ADD3(s1_1, s1_1, two), 2));

    /* tmp[len - 2] = s1 + ((dn + dc) >> 1); */
    STORE(tmp + PARALLEL_COLS_53 * (len - 2) + 0,
          ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
    STORE(tmp + PARALLEL_COLS_53 * (len - 2) + VREG_INT_COUNT,
          ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

    STORE(tmp + PARALLEL_COLS_53 * (len - 1) + 0, dn_0);
    STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT, dn_1);
  } else {
    STORE(tmp + PARALLEL_COLS_53 * (len - 1) + 0, ADD(s1_0, dc_0));
    STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT, ADD(s1_1, dc_1));
  }

  opj_idwt53_v_final_memcpy(tiledp_col, tmp, len, stride);
}

#undef VREG
#undef LOAD_CST
#undef LOADU
#undef LOAD
#undef STORE
#undef STOREU
#undef ADD
#undef ADD3
#undef SUB
#undef SAR

#endif /* (defined(__SSE2__) || defined(__AVX2__)) &&                          \
          !defined(STANDARD_SLOW_VERSION) */

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
static void opj_idwt3_v_cas0(icoeff_t* tmp, const int32_t sn, const int32_t len,
                             icoeff_t* tiledp_col, const size_t stride) {
  int32_t i, j;
  int32_t d1c, d1n, s1n, s0c, s0n;

  ASSERT(len > 1);

  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */

  s1n = tiledp_col[0];
  d1n = tiledp_col[(size_t)sn * stride];
  s0n = s1n - ((d1n + 1) >> 1);

  for (i = 0, j = 0; i < (len - 3); i += 2, j++) {
    d1c = d1n;
    s0c = s0n;

    s1n = tiledp_col[(size_t)(j + 1) * stride];
    d1n = tiledp_col[(size_t)(sn + j + 1) * stride];

    s0n = s1n - ((d1c + d1n + 2) >> 2);

    tmp[i] = s0c;
    tmp[i + 1] = d1c + ((s0c + s0n) >> 1);
  }

  tmp[i] = s0n;

  if (len & 1) {
    tmp[len - 1] =
        tiledp_col[(size_t)((len - 1) / 2) * stride] - ((d1n + 1) >> 1);
    tmp[len - 2] = d1n + ((s0n + tmp[len - 1]) >> 1);
  } else {
    tmp[len - 1] = d1n + s0n;
  }

  for (i = 0; i < len; ++i) {
    tiledp_col[(size_t)i * stride] = tmp[i];
  }
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
static void opj_idwt3_v_cas1(icoeff_t* tmp, const int32_t sn, const int32_t len,
                             icoeff_t* tiledp_col, const size_t stride) {
  int32_t i, j;
  int32_t s1, s2, dc, dn;
  const icoeff_t* in_even = &tiledp_col[(size_t)sn * stride];
  const icoeff_t* in_odd = &tiledp_col[0];

  ASSERT(len > 2);

  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */

  s1 = in_even[stride];
  dc = in_odd[0] - ((in_even[0] + s1 + 2) >> 2);
  tmp[0] = in_even[0] + dc;
  for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

    s2 = in_even[(size_t)(j + 1) * stride];

    dn = in_odd[(size_t)j * stride] - ((s1 + s2 + 2) >> 2);
    tmp[i] = dc;
    tmp[i + 1] = s1 + ((dn + dc) >> 1);

    dc = dn;
    s1 = s2;
  }
  tmp[i] = dc;
  if (!(len & 1)) {
    dn = in_odd[(size_t)(len / 2 - 1) * stride] - ((s1 + 1) >> 1);
    tmp[len - 2] = s1 + ((dn + dc) >> 1);
    tmp[len - 1] = dn;
  } else {
    tmp[len - 1] = s1 + dc;
  }

  for (i = 0; i < len; ++i) {
    tiledp_col[(size_t)i * stride] = tmp[i];
  }
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void opj_idwt53_v(const opj_dwt_t* dwt, icoeff_t* tiledp_col,
                         size_t stride, int32_t nb_cols) {
  const int32_t sn = dwt->sn;
  const int32_t len = sn + dwt->dn;
  if (dwt->cas == 0) {
    /* If len == 1, unmodified value */

#if (defined(__SSE2__) || defined(__AVX2__))
    if (len > 1 && nb_cols == PARALLEL_COLS_53) {
      /* Same as below general case, except that thanks to SSE2/AVX2 */
      /* we can efficiently process 8/16 columns in parallel */
      opj_idwt53_v_cas0_mcols_SSE2_OR_AVX2(dwt->mem, sn, len, tiledp_col,
                                           stride);
      return;
    }
#endif
    if (len > 1) {
      int32_t c;
      for (c = 0; c < nb_cols; c++, tiledp_col++) {
        opj_idwt3_v_cas0(dwt->mem, sn, len, tiledp_col, stride);
      }
      return;
    }
  } else {
    if (len == 1) {
      int32_t c;
      for (c = 0; c < nb_cols; c++, tiledp_col++) {
        tiledp_col[0] /= 2;
      }
      return;
    }

    if (len == 2) {
      int32_t c;
      icoeff_t* out = dwt->mem;
      for (c = 0; c < nb_cols; c++, tiledp_col++) {
        int32_t i;
        const icoeff_t* in_even = &tiledp_col[(size_t)sn * stride];
        const icoeff_t* in_odd = &tiledp_col[0];

        out[1] = in_odd[0] - ((in_even[0] + 1) >> 1);
        out[0] = in_even[0] + out[1];

        for (i = 0; i < len; ++i) {
          tiledp_col[(size_t)i * stride] = out[i];
        }
      }

      return;
    }

#if (defined(__SSE2__) || defined(__AVX2__))
    if (len > 2 && nb_cols == PARALLEL_COLS_53) {
      /* Same as below general case, except that thanks to SSE2/AVX2 */
      /* we can efficiently process 8/16 columns in parallel */
      opj_idwt53_v_cas1_mcols_SSE2_OR_AVX2(dwt->mem, sn, len, tiledp_col,
                                           stride);
      return;
    }
#endif
    if (len > 2) {
      int32_t c;
      for (c = 0; c < nb_cols; c++, tiledp_col++) {
        opj_idwt3_v_cas1(dwt->mem, sn, len, tiledp_col, stride);
      }
      return;
    }
  }
}

// End of openjp2 code.
// NOLINTEND