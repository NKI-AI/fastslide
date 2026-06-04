// Copyright 2026 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_SLIDE_IMAGE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_SLIDE_IMAGE_H_

#include <stdint.h>

#include "fastslide/c/api.h"
#include "fastslide/c/image.h"
#include "fastslide/c/slide_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @file slide_image.h
/// @brief Per-image (per-series / per-scene) C API.
///
/// A `FastSlideSlideImage` is one navigable pyramid inside a slide file.
/// Obtain one with `fastslide_slide_reader_get_image(reader, index)` and
/// free it with `fastslide_slide_image_free`. Handles are stateless views
/// onto the owning reader: they borrow the reader and must not outlive it.
/// The reusable structs and free helpers (`FastSlideLevelInfo`,
/// `FastSlideChannelMetadata`, `FastSlideSlideProperties`, `FastSlideBounds`,
/// `fastslide_slide_reader_free_channel_metadata`,
/// `fastslide_slide_reader_free_properties`, `fastslide_image_free`) are
/// shared with the reader API.

/// @brief Free a per-image handle.
/// @param image Image handle (may be NULL)
FASTSLIDE_API void fastslide_slide_image_free(FastSlideSlideImage* image);

/// @brief Get the number of pyramid levels in this image.
/// @return Level count, or -1 on failure
FASTSLIDE_API int fastslide_slide_image_get_level_count(
    const FastSlideSlideImage* image);

/// @brief Get dimensions and downsample factor for a single level.
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_level_info(
    const FastSlideSlideImage* image, int level, FastSlideLevelInfo* info);

/// @brief Get the dimensions of a single level.
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_level_dimensions(
    const FastSlideSlideImage* image, int level,
    FastSlideImageDimensions* dimensions);

/// @brief Get the downsample factor for a single level.
/// @return Downsample factor, or -1.0 on failure
FASTSLIDE_API double fastslide_slide_image_get_level_downsample(
    const FastSlideSlideImage* image, int level);

/// @brief Get level-0 dimensions.
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_base_dimensions(
    const FastSlideSlideImage* image, FastSlideImageDimensions* dimensions);

/// @brief Get the native tile size, or {0, 0} if untiled.
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_tile_size(
    const FastSlideSlideImage* image, FastSlideImageDimensions* tile_size);

/// @brief Get the image format (RGB / RGBA / grayscale / spectral).
FASTSLIDE_API FastSlideImageFormat
fastslide_slide_image_get_image_format(const FastSlideSlideImage* image);

/// @brief Get the pixel data type.
FASTSLIDE_API FastSlideDataType
fastslide_slide_image_get_data_type(const FastSlideSlideImage* image);

/// @brief Get this image's channel metadata.
/// @param metadata Output array (allocated by function; free with
/// `fastslide_slide_reader_free_channel_metadata`)
/// @param num_channels Output number of channels
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_channel_metadata(
    const FastSlideSlideImage* image, FastSlideChannelMetadata** metadata,
    int* num_channels);

/// @brief Get this image's physical properties (MPP, objective, ...).
/// @param properties Output properties (free with
/// `fastslide_slide_reader_free_properties`)
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_properties(
    const FastSlideSlideImage* image, FastSlideSlideProperties* properties);

/// @brief Get this image's non-empty bounding box.
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_bounds(
    const FastSlideSlideImage* image, FastSlideBounds* bounds);

/// @brief Get this image's Z/T stack extent.
/// @param info Output stack info
/// @return 1 on success, 0 on failure
FASTSLIDE_API int fastslide_slide_image_get_stack_info(
    const FastSlideSlideImage* image, FastSlideStackInfo* info);

/// @brief Read a rectangular region from this image.
/// @param x Top-left X coordinate (level-native)
/// @param y Top-left Y coordinate (level-native)
/// @param width Region width
/// @param height Region height
/// @param level Pyramid level
/// @param z Focal-plane index (0 = first plane)
/// @param t Time-point index (0 = first time point)
/// @return Image handle (free with `fastslide_image_free`) or NULL.
FASTSLIDE_API FastSlideImage* fastslide_slide_image_read_region_coords(
    const FastSlideSlideImage* image, uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, int level, uint32_t z, uint32_t t);

/// @brief Read a rectangular region from this image.
/// @param region Region specification
/// @return Image handle (free with `fastslide_image_free`) or NULL.
FASTSLIDE_API FastSlideImage* fastslide_slide_image_read_region(
    const FastSlideSlideImage* image, const FastSlideRegionSpec* region);

#ifdef __cplusplus
}
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_SLIDE_IMAGE_H_
