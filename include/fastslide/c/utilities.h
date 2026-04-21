// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_UTILITIES_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_UTILITIES_H_

#include <stddef.h>

#include "fastslide/c/image.h"
#include "fastslide/c/slide_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

///
/// This function performs high-quality image resampling
/// using the Lanczos3 kernel.
/// The input image must have separate planar configuration.
///
/// @param image Input image to resample
/// @param output_width Target output width
/// @param output_height Target output height
/// @return Resampled image handle or NULL on failure
FastSlideImage* fastslide_lanczos_resample(const FastSlideImage* image,
                                           uint32_t output_width,
                                           uint32_t output_height);

/// @brief Resample an image using Lanczos2 algorithm
///
/// This function performs high-quality image resampling
/// using the Lanczos2 kernel.
/// The input image must have separate planar configuration.
///
/// @param image Input image to resample
/// @param output_width Target output width
/// @param output_height Target output height
/// @return Resampled image handle or NULL on failure
FastSlideImage* fastslide_lanczos2_resample(const FastSlideImage* image,
                                            uint32_t output_width,
                                            uint32_t output_height);

/// @brief Resample an image using Cosine-windowed sinc algorithm
///
/// This function performs high-quality image resampling
/// using the Cosine3 kernel.
/// The input image must have separate planar configuration.
///
/// @param image Input image to resample
/// @param output_width Target output width
/// @param output_height Target output height
/// @return Resampled image handle or NULL on failure
FastSlideImage* fastslide_cosine_resample(const FastSlideImage* image,
                                          uint32_t output_width,
                                          uint32_t output_height);

/// @brief Resample an image using average downsampling
///
/// This function performs downsampling by averaging pixels in blocks.
/// The input image must have separate planar configuration.
/// The factor must be a power of two and greater than 0.
///
/// @param image Input image to resample
/// @param factor Downsampling factor (must be power of 2)
/// @return Resampled image handle or NULL on failure
FastSlideImage* fastslide_average_resample(const FastSlideImage* image,
                                           uint32_t factor);

/// @brief Resample an image using 2x2 average downsampling
///
/// Convenience function for 2x2 average downsampling.
/// The input image must have separate planar configuration.
///
/// @param image Input image to resample
/// @return Resampled image handle or NULL on failure
FastSlideImage* fastslide_average_2x2_resample(const FastSlideImage* image);

/// @brief Resample an image using 4x4 average downsampling
///
/// Convenience function for 4x4 average downsampling.
/// The input image must have separate planar configuration.
///
/// @param image Input image to resample
/// @return Resampled image handle or NULL on failure
FastSlideImage* fastslide_average_4x4_resample(const FastSlideImage* image);

/// @brief Resample an image using 8x8 average downsampling
///
/// Convenience function for 8x8 average downsampling.
/// The input image must have separate planar configuration.
///
/// @param image Input image to resample
/// @return Resampled image handle or NULL on failure
FastSlideImage* fastslide_average_8x8_resample(const FastSlideImage* image);

// Example utilities for PNG I/O using lodepng

/// @brief Save an RGB/RGBA image as PNG using lodepng
/// @param image RGB or RGBA image to save (must be uint8 format)
/// @param filename Output PNG filename
/// @return 1 on success, 0 on failure
int fastslide_examples_save_as_png(const FastSlideImage* image,
                                   const char* filename);

/// @brief Load an image from PNG using lodepng
/// @param filename Input PNG filename
/// @return Loaded image as RGB uint8, or NULL if failed
/// @note Returns RGB format (alpha channel is discarded if present)
FastSlideImage* fastslide_examples_load_from_png(const char* filename);

#ifdef __cplusplus
}
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_UTILITIES_H_
