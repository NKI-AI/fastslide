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

#include "fastslide/c/slide_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Opaque registry handle
typedef struct FastSlideRegistry FastSlideRegistry;

// Registry management

/// @brief Initialize slide readers registry
/// @return 1 on success, 0 on failure
int fastslide_registry_initialize(void);

/// @brief Get the global registry instance
/// @return Registry handle or NULL on failure
FastSlideRegistry* fastslide_registry_get_instance(void);

/// @brief Create slide reader from file
/// @param registry Registry handle
/// @param file_path Path to slide file
/// @return Slide reader handle or NULL on failure
FastSlideSlideReader* fastslide_registry_create_reader(
    FastSlideRegistry* registry, const char* file_path);

/// @brief Create slide reader from file (convenience function using global
/// registry)
/// @param file_path Path to slide file
/// @return Slide reader handle or NULL on failure
FastSlideSlideReader* fastslide_create_reader(const char* file_path);

// Utility functions

/// @brief Get supported file extensions
/// @param registry Registry handle
/// @param extensions Output array of extension strings (allocated by function)
/// @param num_extensions Output number of extensions
/// @return 1 on success, 0 on failure
int fastslide_registry_get_supported_extensions(FastSlideRegistry* registry,
                                                char*** extensions,
                                                int* num_extensions);

/// @brief Get supported file extensions (convenience function using global
/// registry)
/// @param extensions Output array of extension strings (allocated by function)
/// @param num_extensions Output number of extensions
/// @return 1 on success, 0 on failure
int fastslide_get_supported_extensions(char*** extensions, int* num_extensions);

/// @brief Free extensions array
/// @param extensions Extensions array
/// @param num_extensions Number of extensions
void fastslide_registry_free_extensions(char** extensions, int num_extensions);

/// @brief Check if file is supported
/// @param registry Registry handle
/// @param file_path Path to file
/// @return 1 if supported, 0 if not supported
int fastslide_registry_is_supported(FastSlideRegistry* registry,
                                    const char* file_path);

/// @brief Check if file is supported
/// (convenience function using global registry)
/// @param file_path Path to file
/// @return 1 if supported, 0 if not supported
int fastslide_is_supported(const char* file_path);

// Error handling

/// @brief Get last error message
/// @return Error message string or NULL if no error
const char* fastslide_get_last_error(void);

/// @brief Clear last error message
void fastslide_clear_last_error(void);

// Version information

/// @brief Get FastSlide version
/// @return Version string
const char* fastslide_get_version(void);

#ifdef __cplusplus
}
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_REGISTRY_H_
