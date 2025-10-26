// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_IMAGE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_IMAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Image format enumeration for C API
typedef enum {
  FASTSLIDE_IMAGE_FORMAT_GRAY = 1,     ///< Single channel grayscale
  FASTSLIDE_IMAGE_FORMAT_RGB = 3,      ///< 3 channels: Red, Green, Blue
  FASTSLIDE_IMAGE_FORMAT_RGBA = 4,     ///< 4 channels: Red, Green, Blue, Alpha
  FASTSLIDE_IMAGE_FORMAT_SPECTRAL = 0  ///< N channels (determined at runtime)
} FastSlideImageFormat;

/// @brief Data type enumeration for pixel values
typedef enum {
  FASTSLIDE_DATA_TYPE_UINT8,    ///< 8-bit unsigned integer
  FASTSLIDE_DATA_TYPE_UINT16,   ///< 16-bit unsigned integer
  FASTSLIDE_DATA_TYPE_INT16,    ///< 16-bit signed integer
  FASTSLIDE_DATA_TYPE_UINT32,   ///< 32-bit unsigned integer
  FASTSLIDE_DATA_TYPE_INT32,    ///< 32-bit signed integer
  FASTSLIDE_DATA_TYPE_FLOAT32,  ///< 32-bit floating point
  FASTSLIDE_DATA_TYPE_FLOAT64   ///< 64-bit floating point
} FastSlideDataType;

/// @brief Planar configuration enumeration
/// @details Describes how pixel data is organized in memory
typedef enum {
  FASTSLIDE_PLANAR_CONFIG_CONTIG = 1,   ///< Interleaved: RGBRGBRGB...
  FASTSLIDE_PLANAR_CONFIG_SEPARATE = 2  ///< Planar: RRR...GGG...BBB...
} FastSlidePlanarConfig;

/// @brief Image dimensions
typedef struct {
  uint32_t width;
  uint32_t height;
} FastSlideImageDimensions;

/// @brief Image coordinate
typedef struct {
  uint32_t x;
  uint32_t y;
} FastSlideImageCoordinate;

/// @brief Image information structure
typedef struct {
  FastSlideImageFormat format;
  FastSlideDataType data_type;
  FastSlidePlanarConfig planar_config;
  uint32_t width;
  uint32_t height;
  uint32_t channels;
  size_t bytes_per_sample;
  size_t data_size;
} FastSlideImageInfo;

/// @brief Opaque image handle
typedef struct FastSlideImage FastSlideImage;

// Factory functions

/// @brief Create RGB image
/// @param dimensions Image dimensions
/// @param data_type Data type
/// @return Image handle or NULL on failure
FastSlideImage* fastslide_image_create_rgb(FastSlideImageDimensions dimensions,
                                           FastSlideDataType data_type);

/// @brief Create RGBA image
/// @param dimensions Image dimensions
/// @param data_type Data type
/// @return Image handle or NULL on failure
FastSlideImage* fastslide_image_create_rgba(FastSlideImageDimensions dimensions,
                                            FastSlideDataType data_type);

/// @brief Create grayscale image
/// @param dimensions Image dimensions
/// @param data_type Data type
/// @return Image handle or NULL on failure
FastSlideImage* fastslide_image_create_grayscale(
    FastSlideImageDimensions dimensions, FastSlideDataType data_type);

/// @brief Create spectral/hyperspectral image
/// @param dimensions Image dimensions
/// @param channels Number of channels
/// @param data_type Data type
/// @return Image handle or NULL on failure
FastSlideImage* fastslide_image_create_spectral(
    FastSlideImageDimensions dimensions, uint32_t channels,
    FastSlideDataType data_type);

/// @brief Create blank/uninitialized image that adapts to first paste
/// @param dimensions Image dimensions
/// @return Blank image handle or NULL on failure
FastSlideImage* fastslide_image_create_blank(
    FastSlideImageDimensions dimensions);

/// @brief Create solid color RGB image
/// @param dimensions Image dimensions
/// @param data_type Data type
/// @param red Red component (0-255 for uint8, scaled for other types)
/// @param green Green component
/// @param blue Blue component
/// @return RGB image filled with specified color or NULL on failure
FastSlideImage* fastslide_image_create_solid_color(
    FastSlideImageDimensions dimensions, FastSlideDataType data_type,
    uint32_t red, uint32_t green, uint32_t blue);

// Property accessors

/// @brief Get image information
/// @param image Image handle
/// @param info Output image information
/// @return 1 on success, 0 on failure
int fastslide_image_get_info(const FastSlideImage* image,
                             FastSlideImageInfo* info);

/// @brief Get image dimensions
/// @param image Image handle
/// @param dimensions Output dimensions
/// @return 1 on success, 0 on failure
int fastslide_image_get_dimensions(const FastSlideImage* image,
                                   FastSlideImageDimensions* dimensions);

/// @brief Get image width
/// @param image Image handle
/// @return Image width or 0 on failure
uint32_t fastslide_image_get_width(const FastSlideImage* image);

/// @brief Get image height
/// @param image Image handle
/// @return Image height or 0 on failure
uint32_t fastslide_image_get_height(const FastSlideImage* image);

/// @brief Get number of channels
/// @param image Image handle
/// @return Number of channels or 0 on failure
uint32_t fastslide_image_get_channels(const FastSlideImage* image);

/// @brief Get image format
/// @param image Image handle
/// @return Image format
FastSlideImageFormat fastslide_image_get_format(const FastSlideImage* image);

/// @brief Get data type
/// @param image Image handle
/// @return Data type
FastSlideDataType fastslide_image_get_data_type(const FastSlideImage* image);

/// @brief Get planar configuration
/// @param image Image handle
/// @return Planar configuration
FastSlidePlanarConfig fastslide_image_get_planar_config(
    const FastSlideImage* image);

/// @brief Get bytes per sample
/// @param image Image handle
/// @return Bytes per sample or 0 on failure
size_t fastslide_image_get_bytes_per_sample(const FastSlideImage* image);

/// @brief Check if image is empty
/// @param image Image handle
/// @return 1 if empty, 0 if not empty or on failure
int fastslide_image_is_empty(const FastSlideImage* image);

/// @brief Check if image is initialized
/// @param image Image handle
/// @return 1 if initialized, 0 if not initialized or on failure
int fastslide_image_is_initialized(const FastSlideImage* image);

/// @brief Get total size in bytes
/// @param image Image handle
/// @return Total size in bytes or 0 on failure
size_t fastslide_image_get_size_bytes(const FastSlideImage* image);

/// @brief Get total number of pixels
/// @param image Image handle
/// @return Total number of pixels or 0 on failure
size_t fastslide_image_get_pixel_count(const FastSlideImage* image);

// Data access

/// @brief Get raw data pointer
/// @param image Image handle
/// @return Pointer to raw data or NULL on failure
const uint8_t* fastslide_image_get_data(const FastSlideImage* image);

/// @brief Get mutable raw data pointer
/// @param image Image handle
/// @return Pointer to raw data or NULL on failure
uint8_t* fastslide_image_get_data_mutable(FastSlideImage* image);

/// @brief Copy image data to buffer
/// @param image Image handle
/// @param buffer Output buffer (must be large enough)
/// @param buffer_size Size of output buffer
/// @return 1 on success, 0 on failure
int fastslide_image_copy_data(const FastSlideImage* image, uint8_t* buffer,
                              size_t buffer_size);

// Conversion methods

/// @brief Convert to RGB format
/// @param image Image handle
/// @return RGB image handle or NULL on failure
FastSlideImage* fastslide_image_to_rgb(const FastSlideImage* image);

/// @brief Convert to grayscale format
/// @param image Image handle
/// @return Grayscale image handle or NULL on failure
FastSlideImage* fastslide_image_to_grayscale(const FastSlideImage* image);

/// @brief Convert to planar (separate) memory layout
/// @param image Image handle
/// @return Image with planar layout (RRR...GGG...BBB...) or NULL on failure
FastSlideImage* fastslide_image_to_planar(const FastSlideImage* image);

/// @brief Convert to interleaved (contiguous) memory layout
/// @param image Image handle
/// @return Image with interleaved layout (RGBRGBRGB...) or NULL on failure
FastSlideImage* fastslide_image_to_interleaved(const FastSlideImage* image);

/// @brief Extract specific channels
/// @param image Image handle
/// @param channel_indices Array of channel indices
/// @param num_channels Number of channels to extract
/// @return New image with extracted channels or NULL on failure
FastSlideImage* fastslide_image_extract_channels(
    const FastSlideImage* image, const uint32_t* channel_indices,
    uint32_t num_channels);

/// @brief Clone the image
/// @param image Image handle
/// @return Deep copy of the image or NULL on failure
FastSlideImage* fastslide_image_clone(const FastSlideImage* image);

// Image operations

/// @brief Paste another image onto this image at specified coordinates
/// @param dest_image Destination image to paste onto
/// @param source_image Source image to paste from
/// @param dest_x Destination x coordinate (left edge)
/// @param dest_y Destination y coordinate (top edge)
/// @param source_x Source x coordinate to start copying from (default: 0)
/// @param source_y Source y coordinate to start copying from (default: 0)
/// @param source_width Width of source region to copy (0 = full width)
/// @param source_height Height of source region to copy (0 = full height)
/// @return 1 on success, 0 on failure
int fastslide_image_paste(FastSlideImage* dest_image,
                          const FastSlideImage* source_image, uint32_t dest_x,
                          uint32_t dest_y, uint32_t source_x, uint32_t source_y,
                          uint32_t source_width, uint32_t source_height);

/// @brief Get description string
/// @param image Image handle
/// @param buffer Output buffer for description
/// @param buffer_size Size of output buffer
/// @return Length of description string or -1 on failure
int fastslide_image_get_description(const FastSlideImage* image, char* buffer,
                                    size_t buffer_size);

// Memory management

/// @brief Free image handle
/// @param image Image handle
void fastslide_image_free(FastSlideImage* image);

#ifdef __cplusplus
}
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_IMAGE_H_
