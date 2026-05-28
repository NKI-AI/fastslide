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

#pragma once

#include <stdint.h>

// NOTE: This header replaces the legacy public C API header (`libisyntax.h`)
// after we remove that API. These types are still used across the internal C
// and C++ translation units.

#define DWT_COEFF_BITS 16
#if (DWT_COEFF_BITS == 16)
typedef int16_t icoeff_t;
#else
typedef int32_t icoeff_t;
#endif

#define ISYNTAX_IDWT_PAD_L 4
#define ISYNTAX_IDWT_PAD_R 4
#define ISYNTAX_IDWT_FIRST_VALID_PIXEL 7

#define ISYNTAX_ADJ_TILE_TOP_LEFT 0x100
#define ISYNTAX_ADJ_TILE_TOP_CENTER 0x80
#define ISYNTAX_ADJ_TILE_TOP_RIGHT 0x40
#define ISYNTAX_ADJ_TILE_CENTER_LEFT 0x20
#define ISYNTAX_ADJ_TILE_CENTER 0x10
#define ISYNTAX_ADJ_TILE_CENTER_RIGHT 8
#define ISYNTAX_ADJ_TILE_BOTTOM_LEFT 4
#define ISYNTAX_ADJ_TILE_BOTTOM_CENTER 2
#define ISYNTAX_ADJ_TILE_BOTTOM_RIGHT 1

enum isyntax_pixel_format_t {
  _ISYNTAX_PIXEL_FORMAT_START = 0x100,
  ISYNTAX_PIXEL_FORMAT_RGBA,
  ISYNTAX_PIXEL_FORMAT_BGRA,
  _ISYNTAX_PIXEL_FORMAT_END,
};

typedef int32_t isyntax_open_flags_t;

enum isyntax_open_flags_enum {
  // Initialize internal coefficient allocators during open.
  ISYNTAX_OPEN_FLAG_INIT_ALLOCATORS = 1,

  // Only read barcode then abort early (still treated as success).
  ISYNTAX_OPEN_FLAG_READ_BARCODE_ONLY = 2,

  // Dump the raw XML header to a .xml file (debug).
  ISYNTAX_OPEN_FLAG_DUMP_XML_HEADER = 4,
};

#ifdef __cplusplus

#include <cstddef>
#include <span>

namespace isyntax {

// C++ convenience constants mirroring the legacy C macros.
inline constexpr int kIdwtPadL = ISYNTAX_IDWT_PAD_L;
inline constexpr int kIdwtPadR = ISYNTAX_IDWT_PAD_R;
inline constexpr int kIdwtFirstValidPixel = ISYNTAX_IDWT_FIRST_VALID_PIXEL;

// Span helpers for coefficient blocks.
inline std::span<icoeff_t> CoeffSpan(icoeff_t* ptr, std::size_t count) {
  return std::span<icoeff_t>(ptr, count);
}

inline std::span<const icoeff_t> CoeffSpan(const icoeff_t* ptr,
                                           std::size_t count) {
  return std::span<const icoeff_t>(ptr, count);
}

}  // namespace isyntax

#endif  // __cplusplus
