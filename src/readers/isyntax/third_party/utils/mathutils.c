/*
  BSD 2-Clause License

  Copyright (c) 2019-2024, Pieter Valkema

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "readers/isyntax/third_party/platform/common.h"
#define MATHUTILS_IMPL
#include "readers/isyntax/third_party/utils/mathutils.h"

rect2i clip_rect(rect2i* first, rect2i* second) {
  int32_t x0 = MAX(first->x, second->x);
  int32_t y0 = MAX(first->y, second->y);
  int32_t x1 = MIN(first->x + first->w, second->x + second->w);
  int32_t y1 = MIN(first->y + first->h, second->y + second->h);
  rect2i result = {
      .x = x0,
      .y = y0,
      .w = x1 - x0,
      .h = y1 - y0,
  };
  return result;
}

bounds2i clip_bounds2i(bounds2i a, bounds2i b) {
  bounds2i result = {0};
  result.left = MIN(b.right, MAX(a.left, b.left));
  result.top = MIN(b.bottom, MAX(a.top, b.top));
  result.right = MAX(b.left, MIN(a.right, b.right));
  result.bottom = MAX(b.top, MIN(a.bottom, b.bottom));
  return result;
}

bounds2f clip_bounds2f(bounds2f a, bounds2f b) {
  bounds2f result = {0};
  result.left = MIN(b.right, MAX(a.left, b.left));
  result.top = MIN(b.bottom, MAX(a.top, b.top));
  result.right = MAX(b.left, MIN(a.right, b.right));
  result.bottom = MAX(b.top, MIN(a.bottom, b.bottom));
  return result;
}
