// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_REGISTRY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_REGISTRY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fastslide/c/api.h"
#include "fastslide/c/slide_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Opaque registry handle
typedef struct FastSlideRegistry FastSlideRegistry;

// Registry management

/// @brief Initialize slide readers registry
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_registry_initialize(void);

/// @brief Get the global registry instance
/// @return Registry handle or NULL on failure
FASTSLIDE_API FastSlideRegistry* fastslide_registry_get_instance(void);

/// @brief Create slide reader from file
/// @param registry Registry handle
/// @param file_path Path to slide file
/// @return Slide reader handle or NULL on failure
FASTSLIDE_API FastSlideSlideReader* fastslide_registry_create_reader(
    FastSlideRegistry* registry, const char* file_path);

/// @brief Create slide reader from file (convenience function using global
/// registry)
/// @param file_path Path to slide file
/// @return Slide reader handle or NULL on failure
FASTSLIDE_API FastSlideSlideReader* fastslide_create_reader(
    const char* file_path);

/// @brief Options controlling how a slide is opened.
///
/// Zero-initialize (e.g. `FastSlideOpenOptions options = {0};`) for defaults:
/// no ICC color management. When `apply_icc` is non-zero and the slide carries
/// an embedded ICC profile, `read_region` returns pixels already converted to
/// `target_color_space` using `rendering_intent`.
typedef struct {
  int apply_icc;  ///< Non-zero to apply the embedded ICC profile on read.
  FastSlideColorSpace target_color_space;     ///< Target space (sRGB default).
  FastSlideRenderingIntent rendering_intent;  ///< Rendering intent.
  int icc_use_lut;  ///< Non-zero to build the 256^3 8-bit LUT fast path.
} FastSlideOpenOptions;

/// @brief Create a slide reader with open options (e.g. ICC color management).
///
/// Equivalent to `fastslide_create_reader` plus, when `options->apply_icc` is
/// set, enabling the ICC transform (see
/// `fastslide_slide_reader_enable_icc_transform`). A null `options` behaves
/// like `fastslide_create_reader`.
///
/// @param file_path Path to slide file
/// @param options Open options, or NULL for defaults
/// @return Slide reader handle or NULL on failure
FASTSLIDE_API FastSlideSlideReader* fastslide_create_reader_with_options(
    const char* file_path, const FastSlideOpenOptions* options);

/// @brief Create a slide reader with a per-reader LRU tile cache attached.
///
/// Equivalent to `fastslide_create_reader` followed by
/// `fastslide_slide_reader_set_cache(reader, cache_capacity_bytes)`. A
/// `cache_capacity_bytes` of 0 behaves like `fastslide_create_reader` (no
/// cache).
///
/// @param file_path Path to slide file
/// @param cache_capacity_bytes Cache capacity in bytes (0 = no cache)
/// @return Slide reader handle or NULL on failure
FASTSLIDE_API FastSlideSlideReader* fastslide_create_reader_with_cache(
    const char* file_path, size_t cache_capacity_bytes);

// Global tile cache

/// @brief Resize the process-wide global tile cache.
///
/// Replaces the global cache with a new LRU cache of the requested capacity,
/// dropping any currently cached tiles. Readers attached via
/// `fastslide_slide_reader_use_global_cache` share this cache.
///
/// @param capacity_bytes New global cache capacity in bytes (must be > 0)
/// @return 1 on success, 0 on failure.
FASTSLIDE_API int fastslide_global_cache_set_capacity_bytes(
    size_t capacity_bytes);

/// @brief Read the global tile cache's statistics.
/// @param out_stats Output statistics (must be non-null)
/// @return 1 on success, 0 on invalid arguments.
FASTSLIDE_API int fastslide_global_cache_get_stats(
    FastSlideCacheStats* out_stats);

/// @brief Clear all tiles from the process-wide global tile cache.
FASTSLIDE_API void fastslide_global_cache_clear(void);

// Utility functions

/// @brief Get supported file extensions
/// @param registry Registry handle
/// @param extensions Output array of extension strings (allocated by function)
/// @param num_extensions Output number of extensions
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_registry_get_supported_extensions(
    FastSlideRegistry* registry, char*** extensions, int* num_extensions);

/// @brief Get supported file extensions (convenience function using global
/// registry)
/// @param extensions Output array of extension strings (allocated by function)
/// @param num_extensions Output number of extensions
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_get_supported_extensions(char*** extensions,
                                                     int* num_extensions);

/// @brief Free extensions array
/// @param extensions Extensions array
/// @param num_extensions Number of extensions
FASTSLIDE_API void fastslide_registry_free_extensions(char** extensions,
                                                      int num_extensions);

/// @brief Check if file is supported
/// @param registry Registry handle
/// @param file_path Path to file
/// @return 1 if supported, 0 if not supported
FASTSLIDE_API int fastslide_registry_is_supported(FastSlideRegistry* registry,
                                                  const char* file_path);

/// @brief Check if file is supported
/// (convenience function using global registry)
/// @param file_path Path to file
/// @return 1 if supported, 0 if not supported
FASTSLIDE_API int fastslide_is_supported(const char* file_path);

// Error handling

/// @brief Get last error message
/// @return Error message string or NULL if no error
FASTSLIDE_API const char* fastslide_get_last_error(void);

/// @brief Clear last error message
FASTSLIDE_API void fastslide_clear_last_error(void);

// Version information

/// @brief Get FastSlide version
/// @return Version string
FASTSLIDE_API const char* fastslide_get_version(void);

#ifdef __cplusplus
}
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_REGISTRY_H_
