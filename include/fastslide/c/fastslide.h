// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_FASTSLIDE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_FASTSLIDE_H_

/// @file fastslide.h
/// @brief Complete FastSlide C API
///
/// This header provides a complete C interface to the FastSlide library,
/// mirroring the C++ API structure. The C API follows these conventions:
///
/// - All functions return 1 on success, 0 on failure (unless otherwise noted)
/// - Error messages can be retrieved using fastslide_get_last_error()
/// - Memory allocated by the library must be freed using appropriate free
/// functions
/// - All handles are opaque pointers and should not be accessed directly
///
/// @section usage Basic Usage
///
/// ```c
/// #include "fastslide/c/fastslide.h"
///
/// // Initialize the library
/// fastslide_registry_initialize();
///
/// // Create a slide reader
/// FastSlideSlideReader* reader = fastslide_create_reader("slide.svs");
/// if (!reader) {
///     printf("Error: %s\n", fastslide_get_last_error());
///     return 1;
/// }
///
/// // Read a region
/// FastSlideImage* image = fastslide_slide_reader_read_region_coords(
///     reader, 0, 0, 1024, 1024, 0);
///
/// // Cleanup
/// fastslide_image_free(image);
/// fastslide_slide_reader_free(reader);
/// ```

#include "fastslide/c/image.h"
#include "fastslide/c/registry.h"
#include "fastslide/c/slide_reader.h"
#include "fastslide/c/utilities.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief FastSlide C API version
#define FASTSLIDE_C_API_VERSION_MAJOR 1
#define FASTSLIDE_C_API_VERSION_MINOR 0
#define FASTSLIDE_C_API_VERSION_PATCH 0

/// @brief Get C API version string
/// @return Version string in format "major.minor.patch"
const char* fastslide_c_api_get_version(void);

/// @brief Initialize FastSlide library
/// @details This function initializes the slide readers registry and sets up
///          error handling. It should be called before using any other
///          FastSlide functions.
/// @return 1 on success, 0 on failure
int fastslide_initialize(void);

/// @brief Cleanup FastSlide library
/// @details This function cleans up global resources. Call this when done
///          with the FastSlide library.
void fastslide_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_FASTSLIDE_H_
