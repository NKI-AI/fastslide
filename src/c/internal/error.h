// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_SRC_C_INTERNAL_ERROR_H_
#define AIFO_FASTSLIDE_SRC_C_INTERNAL_ERROR_H_

/// @file error.h
/// @brief Shared error / null-check helpers for the C API translation units.
///
/// The C-API entrypoints in `src/c/slide_reader.cpp`, `src/c/registry.cpp`
/// and `src/c/image.cpp` all repeat the same pattern:
///
/// 1. Validate that the opaque handle (`reader`, `image`, …) is non-null and
///    its inner `unique_ptr` is still alive.
/// 2. Validate that the caller-supplied output pointers are non-null.
/// 3. On failure, set the per-thread last-error string and return a sentinel.
///
/// The macros defined here express each of those steps in one line.

// Forward declaration of the internal last-error setter implemented in
// `src/c/registry.cpp`. The setter is intentionally not exported in any
// public C header.
extern "C" void fastslide_set_last_error(const char* message);

namespace fastslide::c::internal {

/// @brief Thin C++ shim around the C-API last-error setter.
inline void SetLastError(const char* message) {
  fastslide_set_last_error(message);
}

}  // namespace fastslide::c::internal

/// @brief Validate a `FastSlideSlideReader*`-style handle and return on miss.
///
/// Both `handle` and `handle->reader` (the wrapped `unique_ptr`) must be live.
/// Otherwise sets the canonical "reader is null or closed" message and
/// returns @p retval from the caller.
#define FASTSLIDE_REQUIRE_READER(handle, retval)                          \
  do {                                                                    \
    if (!(handle) || !(handle)->reader) {                                 \
      ::fastslide::c::internal::SetLastError("reader is null or closed"); \
      return retval;                                                      \
    }                                                                     \
  } while (0)

/// @brief Validate that a caller-supplied output pointer is non-null.
///
/// Sets the message `"<name> cannot be null"` and returns @p retval if not.
#define FASTSLIDE_REQUIRE_NOT_NULL(ptr, name, retval)                 \
  do {                                                                \
    if (!(ptr)) {                                                     \
      ::fastslide::c::internal::SetLastError(name " cannot be null"); \
      return retval;                                                  \
    }                                                                 \
  } while (0)

#endif  // AIFO_FASTSLIDE_SRC_C_INTERNAL_ERROR_H_
