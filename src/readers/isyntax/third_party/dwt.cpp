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

#include <cstddef>
#include <span>

#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/platform/intrinsics.h"

#include "fastslide/readers/isyntax/third_party/dwt.h"

namespace isyntax {
namespace dwt {

void Idwt53(std::span<icoeff_t> idwt, int32_t quadrant_width,
            int32_t quadrant_height) {
  const int32_t full_width = quadrant_width * 2;
  const int32_t full_height = quadrant_height * 2;
  const int32_t idwt_stride = full_width;
  ASSERT(static_cast<size_t>(full_width) * static_cast<size_t>(full_height) <=
         idwt.size());

  // Horizontal pass
  opj_dwt_t horizontal = {0};
  const size_t dwt_mem_size =
      (static_cast<size_t>(MAX(quadrant_width, quadrant_height)) * 2) *
      static_cast<size_t>(PARALLEL_COLS_53) * sizeof(icoeff_t);

  horizontal.mem = reinterpret_cast<icoeff_t*>(
      alloca(dwt_mem_size));       // TODO(jonasteuwen): align?
  horizontal.sn = quadrant_width;  // number of elements in low pass band
  horizontal.dn = quadrant_width;  // number of elements in high pass band
  horizontal.cas = 1;

  for (int32_t row = 0; row < full_height; ++row) {
    icoeff_t* input_row = idwt.data() + row * idwt_stride;
    opj_idwt53_h(&horizontal, input_row);
  }

  // Vertical pass
  opj_dwt_t vertical = {0};
  vertical.mem = horizontal.mem;
  vertical.sn = quadrant_height;  // number of elements in low pass band
  vertical.dn = quadrant_height;  // number of elements in high pass band
  vertical.cas = 1;

  int32_t col = 0;
  const int32_t last_col = full_width;
  for (; col + PARALLEL_COLS_53 <= last_col; col += PARALLEL_COLS_53) {
    opj_idwt53_v(&vertical, idwt.data() + col, idwt_stride, PARALLEL_COLS_53);
  }
  if (col < last_col) {
    opj_idwt53_v(&vertical, idwt.data() + col, idwt_stride, (last_col - col));
  }
}

}  // namespace dwt
}  // namespace isyntax
