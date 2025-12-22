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
#ifndef COMMON_H
#define COMMON_H

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

// Platform detection
#ifdef _WIN32
#define WINDOWS 1
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WINVER 0x0600
#define OPENGL_H <glad/glad.h>
#define PATH_SEP "\\"
#else
#define WINDOWS 0
#define PATH_SEP "/"
#endif

#ifdef __APPLE__
#define APPLE 1
#include <TargetConditionals.h>
#if TARGET_CPU_ARM64
#define APPLE_ARM 1
#else
#define APPLE_ARM 0
#endif
#define OPENGL_H <OpenGL/gl3.h>
#else
#define APPLE 0
#define APPLE_ARM 0
#endif

#if defined(__linux__) || \
    (!defined(__APPLE__) && (defined(__unix__) || defined(_POSIX_VERSION)))
#include <features.h>
#endif
#if !WINDOWS
#include <stdlib.h>
#include <unistd.h>  // for access(), F_OK
// #define _aligned_malloc(size, alignment) aligned_alloc(alignment, size)
// #define _aligned_free(ptr) free(ptr)
#endif

#if defined(__linux__) || \
    (!defined(__APPLE__) && (defined(__unix__) || defined(_POSIX_VERSION)))
#define LINUX 1
#define OPENGL_H <GL/glew.h>
#else
#define LINUX 0
#endif

// Compiler detection
#ifdef _MSC_VER
#define COMPILER_MSVC 1
#else
#define COMPILER_MSVC 0
#endif

#ifdef __GNUC__
#define COMPILER_GCC 1
#else
#define COMPILER_GCC 0
#endif

// IDE detection (for dealing with pesky preprocessor highlighting issues)
#if defined(__JETBRAINS_IDE__)
#define CODE_EDITOR 1
#else
#define CODE_EDITOR 0
#endif

// Use 64-bit file offsets for fopen, etc.
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if COMPILER_MSVC
#include <io.h>
#define access _access
#define F_OK 0  // check for file existence
#define S_ISDIR(m) \
  (((m)&0xF000) == \
   0x4000)  // check for whether a file is a directory (from stat.h)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#define alloca _alloca
#define fseeko64 _fseeki64
#define fopen64 fopen
#endif

#ifndef THREAD_LOCAL
#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL __thread
#endif
#endif

#if LINUX
#include <fcntl.h>
#endif

// adapted from lz4.c
#ifndef FORCE_INLINE
#ifdef _MSC_VER /* Visual Studio */
#define FORCE_INLINE static __forceinline
#else
#if defined(__cplusplus) || \
    defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L /* C99 */
#ifdef __GNUC__
#define FORCE_INLINE static inline __attribute__((always_inline))
#else
#define FORCE_INLINE static inline
#endif
#else
#define FORCE_INLINE static
#endif /* __STDC_VERSION__ */
#endif /* _MSC_VER */
#endif /* LZ4_FORCE_INLINE */

// Wrappers for using libc versions of malloc(), realloc() and free(), if you
// really need to. You can use these if you replaced regular malloc with ltalloc
// (see below).
FORCE_INLINE void* libc_malloc(size_t size) {
  return malloc(size);
}

FORCE_INLINE void* libc_realloc(void* memory, size_t new_size) {
  return realloc(memory, new_size);
}

FORCE_INLINE void libc_free(void* memory) {
  free(memory);
}

#define IS_LTALLOC_AVAILABLE 0

typedef int32_t bool32;
typedef int8_t bool8;

// String type with a known size (don't assume zero-termination)
typedef struct str_t {
  const char* s;
  size_t len;
} str_t;

// Convenience macros
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#define ATLEAST(a, b) MAX(a, b)
#define ATMOST(a, b) MIN(a, b)
#define ABS(x) ((x) < 0 ? -(x) : (x))

#ifdef __cplusplus
#define LERP(t, a, b) ((a) + (t) * static_cast<float>((b) - (a)))
#define UNLERP(t, a, b) (((t) - (a)) / static_cast<float>((b) - (a)))
#else
#define LERP(t, a, b) ((a) + (t) * (1.0f * ((b) - (a))))
#define UNLERP(t, a, b) (((t) - (a)) / (1.0f * ((b) - (a))))
#endif

#define CLAMP(x, xmin, xmax) \
  ((x) < (xmin) ? (xmin) : (x) > (xmax) ? (xmax) : (x))

#define MACRO_VAR(name) concat(name, __LINE__)
#define defer(start, end)                                \
  for (int MACRO_VAR(_i_) = (start, 0); !MACRO_VAR(_i_); \
       (MACRO_VAR(_i_) += 1, end))

#define SQUARE(x) ((x) * (x))

#define memset_zero(x) memset((x), 0, sizeof(*x))

#define KILOBYTES(n) (1024LL * (n))
#define MEGABYTES(n) (1024LL * KILOBYTES(n))
#define GIGABYTES(n) (1024LL * MEGABYTES(n))
#define TERABYTES(n) (1024LL * GIGABYTES(n))

#if defined(SOURCE_PATH_SIZE) && !defined(__FILENAME__)
#define __FILENAME__ (__FILE__ + SOURCE_PATH_SIZE)
#elif !defined(__FILENAME__)
#define __FILENAME__ __FILE__
#endif

#ifndef NDEBUG
#define DO_DEBUG 1
#if COMPILER_GCC
#define DEBUG_TRAP() __builtin_trap()
#elif COMPILER_MSVC
#define DEBUG_TRAP() __debugbreak()
#else
#define DEBUG_TRAP() abort()
#endif
#define ASSERT(expr) \
  do {               \
    if (!(expr)) {   \
      DEBUG_TRAP();  \
      abort();       \
    }                \
  } while (0)
#else
#define DO_DEBUG 0
#define ASSERT(expr)
#endif

// http://www.pixelbeat.org/programming/gcc/static_assert.html
#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
/* These can't be used after statements in c89. */
#ifdef __COUNTER__
#define STATIC_ASSERT(e)                                                  \
  ;                                                                       \
  enum {                                                                  \
    ASSERT_CONCAT(ASSERT_CONCAT(static_assert_, __COUNTER__), __LINE__) = \
        1 / ((e) ? 1 : 0)                                                 \
  }
#else
/* This can't be used twice on the same line so ensure if using in headers
 * that the headers are not included twice (by wrapping in #ifndef...#endif)
 * Note it doesn't cause an issue when used on same line of separate modules
 * compiled with gcc -combine -fwhole-program.  */
#define STATIC_ASSERT(e) \
  ;                      \
  enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1 / ((e) ? 1 : 0) }
#endif

#endif  // COMMON_H
