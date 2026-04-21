// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_SRC_C_INTERNAL_DEBUG_H_
#define AIFO_FASTSLIDE_SRC_C_INTERNAL_DEBUG_H_

/// @file debug.h
/// @brief Shared debug printing helpers for the C API translation units.
///
/// The C-API surface (`src/c/*.cpp`) used to define `IsDebugEnabled` and the
/// `FASTSLIDE_DEBUG_PRINT` / `FASTSLIDE_ERROR_PRINT` macros separately in
/// every translation unit. This header centralises them so toggling debug
/// output behaviour only requires one change.

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fastslide::c::internal {

/// @brief Returns true if FASTSLIDE_DEBUG=1 is set in the environment.
///
/// The result is cached after the first call (per-translation-unit).
inline bool IsDebugEnabled() {
  static bool checked = false;
  static bool enabled = false;
  if (!checked) {
    const char* debug_env = std::getenv("FASTSLIDE_DEBUG");
    enabled = (debug_env != nullptr && std::strcmp(debug_env, "1") == 0);
    checked = true;
  }
  return enabled;
}

}  // namespace fastslide::c::internal

/// @brief printf when FASTSLIDE_DEBUG=1, no-op otherwise.
#define FASTSLIDE_DEBUG_PRINT(...)                  \
  do {                                              \
    if (::fastslide::c::internal::IsDebugEnabled()) \
      printf(__VA_ARGS__);                          \
  } while (0)

/// @brief Always-printed diagnostic.
#define FASTSLIDE_ERROR_PRINT(...) \
  do {                             \
    printf(__VA_ARGS__);           \
  } while (0)

#endif  // AIFO_FASTSLIDE_SRC_C_INTERNAL_DEBUG_H_
