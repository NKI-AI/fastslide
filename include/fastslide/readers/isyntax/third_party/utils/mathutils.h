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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

typedef struct rect2i {
  int32_t x, y, w, h;
} rect2i;

FORCE_INLINE rect2i RECT2I(int32_t x, int32_t y, int32_t w, int32_t h) {
  rect2i r = {x, y, w, h};
  return r;
}

typedef struct rect2f {
  float x, y, w, h;
} rect2f;

FORCE_INLINE rect2f RECT2F(float x, float y, float w, float h) {
  rect2f r = {x, y, w, h};
  return r;
}

typedef struct v2i {
  int32_t x, y;
} v2i;

FORCE_INLINE v2i V2I(int32_t x, int32_t y) {
  v2i v = {x, y};
  return v;
}

typedef struct rgba_t {
  union {
    struct {
      uint8_t r, g, b, a;
    };

    uint8_t values[4];
  };
} rgba_t;

FORCE_INLINE rgba_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  rgba_t rgba = {{{r, g, b, a}}};
  return rgba;
}

#ifndef V2F_DEFINED
#define V2F_DEFINED

typedef struct v2f {
  float x, y;
} v2f;
#endif
FORCE_INLINE v2f V2F(float x, float y) {
  v2f v = {x, y};
  return v;
}

typedef struct v3f {
  union {
    struct {
      float r, g, b;
    };

    struct {
      float x, y, z;
    };

    float values[3];
  };
} v3f;

FORCE_INLINE v3f V3F(float x, float y, float z) {
  v3f v = {{{x, y, z}}};
  return v;
}

#ifndef V4F_DEFINED
#define V4F_DEFINED

typedef struct v4f {
  union {
    struct {
      float r, g, b, a;
    };

    struct {
      float x, y, z, w;
    };

    float values[4];
  };
} v4f;
#endif
FORCE_INLINE v4f V4F(float x, float y, float z, float w) {
  v4f v = {{{x, y, z, w}}};
  return v;
}

typedef struct bounds2i {
  union {
    struct {
      int32_t left, top, right, bottom;
    };

    struct {
      v2i min, max;
    };
  };
} bounds2i;

FORCE_INLINE bounds2i BOUNDS2I(int32_t left, int32_t top, int32_t right,
                               int32_t bottom) {
  bounds2i b = {{{left, top, right, bottom}}};
  return b;
}

typedef struct bounds2f {
  union {
    struct {
      float left, top, right, bottom;
    };

    struct {
      v2f min, max;
    };
  };
} bounds2f;

#pragma pack(pop)

// globals
#if defined(MATHUTILS_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

//...

#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif
