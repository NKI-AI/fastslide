// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_SLIDE_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_SLIDE_READER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fastslide/c/image.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Opaque slide reader handle
typedef struct FastSlideSlideReader FastSlideSlideReader;

/// @brief Property value types
typedef enum {
  FASTSLIDE_PROPERTY_TYPE_STRING,
  FASTSLIDE_PROPERTY_TYPE_SIZE_T,
  FASTSLIDE_PROPERTY_TYPE_DOUBLE
} FastSlidePropertyType;

/// @brief Property value union
typedef struct {
  FastSlidePropertyType type;

  union {
    const char* string_value;
    size_t size_t_value;
    double double_value;
  };
} FastSlidePropertyValue;

/// @brief Level information
typedef struct {
  FastSlideImageDimensions dimensions;
  double downsample_factor;
} FastSlideLevelInfo;

/// @brief Slide properties
typedef struct {
  double mpp_x;                    ///< Microns per pixel X
  double mpp_y;                    ///< Microns per pixel Y
  double objective_magnification;  ///< Objective magnification
  char* objective_name;            ///< Objective name (allocated by function)
  char* scanner_model;             ///< Scanner model (allocated by function)
  char*
      scan_date;  ///< Scan date (allocated by function, NULL if not available)
} FastSlideSlideProperties;

/// @brief Color RGB
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} FastSlideColorRGB;

/// @brief Slide bounds (bounding box of non-empty region)
typedef struct {
  int64_t x;       ///< X coordinate (level 0)
  int64_t y;       ///< Y coordinate (level 0)
  int64_t width;   ///< Width of bounding box
  int64_t height;  ///< Height of bounding box
} FastSlideBounds;

/// @brief Channel metadata
typedef struct {
  char* name;               ///< Channel name (allocated by function)
  char* biomarker;          ///< Biomarker name (allocated by function)
  FastSlideColorRGB color;  ///< Color
  uint32_t exposure_time;   ///< Exposure time
  uint32_t signal_units;    ///< Signal units
} FastSlideChannelMetadata;

/// @brief Region specification
typedef struct {
  FastSlideImageCoordinate top_left;
  FastSlideImageDimensions size;
  int level;
} FastSlideRegionSpec;

// Basic slide properties

/// @brief Get level count
/// @param reader Slide reader handle
/// @return Number of levels or -1 on failure
int fastslide_slide_reader_get_level_count(const FastSlideSlideReader* reader);

/// @brief Get level information
/// @param reader Slide reader handle
/// @param level Level index
/// @param info Output level information
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_level_info(const FastSlideSlideReader* reader,
                                          int level, FastSlideLevelInfo* info);

/// @brief Get level dimensions
/// @param reader Slide reader handle
/// @param level Level index
/// @param dimensions Output dimensions
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_level_dimensions(
    const FastSlideSlideReader* reader, int level,
    FastSlideImageDimensions* dimensions);

/// @brief Get base dimensions (level 0)
/// @param reader Slide reader handle
/// @param dimensions Output dimensions
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_base_dimensions(
    const FastSlideSlideReader* reader, FastSlideImageDimensions* dimensions);

/// @brief Get level downsample factor
/// @param reader Slide reader handle
/// @param level Level index
/// @return Downsample factor or -1.0 on failure
double fastslide_slide_reader_get_level_downsample(
    const FastSlideSlideReader* reader, int level);

/// @brief Get best level for downsample factor
/// @param reader Slide reader handle
/// @param downsample Desired downsample factor
/// @return Best matching level or -1 on failure
int fastslide_slide_reader_get_best_level_for_downsample(
    const FastSlideSlideReader* reader, double downsample);

// Slide properties

/// @brief Get slide properties
/// @param reader Slide reader handle
/// @param properties Output properties (allocated by function)
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_properties(const FastSlideSlideReader* reader,
                                          FastSlideSlideProperties* properties);

/// @brief Free slide properties
/// @param properties Properties to free
void fastslide_slide_reader_free_properties(
    FastSlideSlideProperties* properties);

/// @brief Get slide bounds (bounding box of non-empty region)
/// @param reader Slide reader handle
/// @param bounds Output bounds structure
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_bounds(const FastSlideSlideReader* reader,
                                      FastSlideBounds* bounds);

/// @brief Get format name
/// @param reader Slide reader handle
/// @return Format name string or NULL on failure
/// (pointer valid until reader is freed)
const char* fastslide_slide_reader_get_format_name(
    const FastSlideSlideReader* reader);

/// @brief Get image format
/// @param reader Slide reader handle
/// @return Image format (FASTSLIDE_IMAGE_FORMAT_RGB or
/// FASTSLIDE_IMAGE_FORMAT_SPECTRAL)
FastSlideImageFormat fastslide_slide_reader_get_image_format(
    const FastSlideSlideReader* reader);

/// @brief Get tile size
/// @param reader Slide reader handle
/// @param tile_size Output tile size
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_tile_size(const FastSlideSlideReader* reader,
                                         FastSlideImageDimensions* tile_size);

// Channel metadata

/// @brief Get channel metadata
/// @param reader Slide reader handle
/// @param metadata Output array of channel metadata (allocated by function)
/// @param num_channels Output number of channels
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_channel_metadata(
    const FastSlideSlideReader* reader, FastSlideChannelMetadata** metadata,
    int* num_channels);

/// @brief Free channel metadata array
/// @param metadata Metadata array to free
/// @param num_channels Number of channels
void fastslide_slide_reader_free_channel_metadata(
    FastSlideChannelMetadata* metadata, int num_channels);

// Channel visibility controls

/// @brief Set visible channels
/// @param reader Slide reader handle
/// @param channel_indices Array of channel indices (NULL for all channels)
/// @param num_channels Number of channels (0 for all channels)
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_set_visible_channels(FastSlideSlideReader* reader,
                                                const size_t* channel_indices,
                                                int num_channels);

/// @brief Get visible channels
/// @param reader Slide reader handle
/// @param channel_indices Output array of channel indices
/// (allocated by function)
/// @param num_channels Output number of channels
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_visible_channels(
    const FastSlideSlideReader* reader, size_t** channel_indices,
    int* num_channels);

/// @brief Show all channels
/// @param reader Slide reader handle
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_show_all_channels(FastSlideSlideReader* reader);

/// @brief Free visible channels array
/// @param channel_indices Channel indices array
void fastslide_slide_reader_free_visible_channels(size_t* channel_indices);

// Region reading

/// @brief Read region from slide
/// @param reader Slide reader handle
/// @param region Region specification
/// @return Image handle or NULL on failure
FastSlideImage* fastslide_slide_reader_read_region(
    const FastSlideSlideReader* reader, const FastSlideRegionSpec* region);

/// @brief Read region with coordinates
/// @param reader Slide reader handle
/// @param x Top-left X coordinate
/// @param y Top-left Y coordinate
/// @param width Region width
/// @param height Region height
/// @param level Pyramid level
/// @return Image handle or NULL on failure
FastSlideImage* fastslide_slide_reader_read_region_coords(
    const FastSlideSlideReader* reader, uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, int level);

// Associated images

/// @brief Get associated image names
/// @param reader Slide reader handle
/// @param names Output array of names (allocated by function)
/// @param num_names Output number of names
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_associated_image_names(
    const FastSlideSlideReader* reader, char*** names, int* num_names);

/// @brief Free associated image names
/// @param names Names array
/// @param num_names Number of names
void fastslide_slide_reader_free_associated_image_names(char** names,
                                                        int num_names);

/// @brief Get associated image dimensions
/// @param reader Slide reader handle
/// @param name Associated image name
/// @param dimensions Output dimensions
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_associated_image_dimensions(
    const FastSlideSlideReader* reader, const char* name,
    FastSlideImageDimensions* dimensions);

/// @brief Read associated image
/// @param reader Slide reader handle
/// @param name Associated image name
/// @return Image handle or NULL on failure
FastSlideImage* fastslide_slide_reader_read_associated_image(
    const FastSlideSlideReader* reader, const char* name);

// Metadata access

/// @brief Get metadata property keys
/// @param reader Slide reader handle
/// @param keys Output array of keys (allocated by function)
/// @param num_keys Output number of keys
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_metadata_keys(const FastSlideSlideReader* reader,
                                             char*** keys, int* num_keys);

/// @brief Free metadata keys
/// @param keys Keys array
/// @param num_keys Number of keys
void fastslide_slide_reader_free_metadata_keys(char** keys, int num_keys);

/// @brief Get metadata property value
/// @param reader Slide reader handle
/// @param key Property key
/// @param value Output property value
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_get_metadata_value(
    const FastSlideSlideReader* reader, const char* key,
    FastSlidePropertyValue* value);

/// @brief Get metadata property type
/// @param reader Slide reader handle
/// @param key Property key
/// @return Property type
FastSlidePropertyType fastslide_slide_reader_get_metadata_type(
    const FastSlideSlideReader* reader, const char* key);

// Utility functions

/// @brief Check if region is valid for the slide
/// @param reader Slide reader handle
/// @param region Region specification
/// @return 1 if valid, 0 if invalid
int fastslide_slide_reader_is_region_valid(const FastSlideSlideReader* reader,
                                           const FastSlideRegionSpec* region);

/// @brief Clamp region to slide bounds
/// @param reader Slide reader handle
/// @param region Input region
/// @param clamped_region Output clamped region
/// @return 1 on success, 0 on failure
int fastslide_slide_reader_clamp_region(const FastSlideSlideReader* reader,
                                        const FastSlideRegionSpec* region,
                                        FastSlideRegionSpec* clamped_region);

// Memory management

/// @brief Free slide reader handle
/// @param reader Slide reader handle
void fastslide_slide_reader_free(FastSlideSlideReader* reader);

#ifdef __cplusplus
}
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_SLIDE_READER_H_
