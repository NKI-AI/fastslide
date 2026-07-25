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

//! Raw FFI declarations for the FastSlide C API.
//!
//! This crate is a thin, unsafe, hand-written mirror of the public C headers
//! under `include/fastslide/c/` (`registry.h`, `slide_reader.h`,
//! `slide_image.h`, `image.h`, `utilities.h`, `fastslide.h`). It performs no
//! memory management or error translation of its own; that belongs to the safe
//! `fastslide` crate that wraps it.
//!
//! All opaque handles are `#[repr(C)]` zero-sized structs used only behind
//! pointers. POD structs and enums are `#[repr(C)]` and match the C layout
//! field-for-field. Functions follow the C convention: integer-returning
//! functions yield `1` on success and `0` on failure (or `-1` for counts),
//! pointer-returning functions yield null on failure, and the last error
//! message is available via [`fastslide_get_last_error`].

#![allow(non_camel_case_types)]

use std::os::raw::{c_char, c_double, c_int};

// ===========================================================================
// Opaque handles
// ===========================================================================

#[repr(C)]
pub struct FastSlideSlideReader {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct FastSlideSlideImage {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct FastSlideImage {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct FastSlideRegistry {
    _opaque: [u8; 0],
}

// ===========================================================================
// Enums (numeric values must match include/fastslide/c/*.h exactly)
// ===========================================================================

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FastSlideImageFormat {
    Spectral = 0,
    Gray = 1,
    Rgb = 3,
    Rgba = 4,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FastSlideDataType {
    UInt8 = 0,
    UInt16 = 1,
    Int16 = 2,
    UInt32 = 3,
    Int32 = 4,
    Float32 = 5,
    Float64 = 6,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FastSlidePlanarConfig {
    Contiguous = 1,
    Separate = 2,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FastSlidePropertyType {
    String = 0,
    SizeT = 1,
    Double = 2,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FastSlideColorSpace {
    Rgb = 0,
    Linear = 1,
    Srgb = 2,
    Automatic = 3,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FastSlideRenderingIntent {
    Perceptual = 0,
    RelativeColorimetric = 1,
    Saturation = 2,
    AbsoluteColorimetric = 3,
}

// ===========================================================================
// POD structs
// ===========================================================================

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideImageDimensions {
    pub width: u32,
    pub height: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideImageCoordinate {
    pub x: u32,
    pub y: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideImageInfo {
    pub format: FastSlideImageFormat,
    pub data_type: FastSlideDataType,
    pub planar_config: FastSlidePlanarConfig,
    pub width: u32,
    pub height: u32,
    pub channels: u32,
    pub bytes_per_sample: usize,
    pub data_size: usize,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideLevelInfo {
    pub dimensions: FastSlideImageDimensions,
    pub downsample_factor: c_double,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideSlideProperties {
    pub mpp_x: c_double,
    pub mpp_y: c_double,
    pub objective_magnification: c_double,
    pub objective_name: *mut c_char,
    pub scanner_model: *mut c_char,
    pub scan_date: *mut c_char,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideColorRGB {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideBounds {
    pub x: i64,
    pub y: i64,
    pub width: i64,
    pub height: i64,
}

#[repr(C)]
#[derive(Debug)]
pub struct FastSlideChannelMetadata {
    pub name: *mut c_char,
    pub biomarker: *mut c_char,
    pub color: FastSlideColorRGB,
    pub exposure_time: u32,
    pub signal_units: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideRegionSpec {
    pub top_left: FastSlideImageCoordinate,
    pub size: FastSlideImageDimensions,
    pub level: c_int,
    pub z: u32,
    pub t: u32,
}

/// Z (focal) / T (time) stack extent for an image. `z_count`/`t_count` are
/// always >= 1; the spacings are valid only when the matching `has_*` flag is
/// non-zero.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideStackInfo {
    pub z_count: u32,
    pub t_count: u32,
    pub has_z_spacing: c_int,
    pub z_spacing_um: c_double,
    pub has_t_interval: c_int,
    pub t_interval_s: c_double,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union FastSlidePropertyValueUnion {
    pub string_value: *const c_char,
    pub size_t_value: usize,
    pub double_value: c_double,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FastSlidePropertyValue {
    pub type_: FastSlidePropertyType,
    pub value: FastSlidePropertyValueUnion,
}

/// Open options mirroring `FastSlideOpenOptions` in `registry.h`. Zero-fill
/// (`Default`) for no color management.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FastSlideOpenOptions {
    pub apply_icc: c_int,
    pub target_color_space: FastSlideColorSpace,
    pub rendering_intent: FastSlideRenderingIntent,
    pub icc_use_lut: c_int,
}

// ===========================================================================
// C API
// ===========================================================================

unsafe extern "C" {
    // ---- fastslide.h (library lifecycle / version) ----
    pub fn fastslide_c_api_get_version() -> *const c_char;
    pub fn fastslide_initialize() -> c_int;
    pub fn fastslide_cleanup();

    // ---- registry.h ----
    pub fn fastslide_registry_initialize() -> c_int;
    pub fn fastslide_registry_get_instance() -> *mut FastSlideRegistry;
    pub fn fastslide_registry_create_reader(
        registry: *mut FastSlideRegistry,
        file_path: *const c_char,
    ) -> *mut FastSlideSlideReader;
    pub fn fastslide_create_reader(file_path: *const c_char) -> *mut FastSlideSlideReader;
    pub fn fastslide_create_reader_with_options(
        file_path: *const c_char,
        options: *const FastSlideOpenOptions,
    ) -> *mut FastSlideSlideReader;
    pub fn fastslide_registry_get_supported_extensions(
        registry: *mut FastSlideRegistry,
        extensions: *mut *mut *mut c_char,
        num_extensions: *mut c_int,
    ) -> c_int;
    pub fn fastslide_get_supported_extensions(
        extensions: *mut *mut *mut c_char,
        num_extensions: *mut c_int,
    ) -> c_int;
    pub fn fastslide_registry_free_extensions(extensions: *mut *mut c_char, num_extensions: c_int);
    pub fn fastslide_registry_is_supported(
        registry: *mut FastSlideRegistry,
        file_path: *const c_char,
    ) -> c_int;
    pub fn fastslide_is_supported(file_path: *const c_char) -> c_int;
    pub fn fastslide_get_last_error() -> *const c_char;
    pub fn fastslide_clear_last_error();
    pub fn fastslide_get_version() -> *const c_char;

    // ---- slide_reader.h: pyramid info ----
    pub fn fastslide_slide_reader_get_level_count(reader: *const FastSlideSlideReader) -> c_int;
    pub fn fastslide_slide_reader_get_level_info(
        reader: *const FastSlideSlideReader,
        level: c_int,
        info: *mut FastSlideLevelInfo,
    ) -> c_int;
    pub fn fastslide_slide_reader_get_level_dimensions(
        reader: *const FastSlideSlideReader,
        level: c_int,
        dimensions: *mut FastSlideImageDimensions,
    ) -> c_int;
    pub fn fastslide_slide_reader_get_base_dimensions(
        reader: *const FastSlideSlideReader,
        dimensions: *mut FastSlideImageDimensions,
    ) -> c_int;
    pub fn fastslide_slide_reader_get_level_downsample(
        reader: *const FastSlideSlideReader,
        level: c_int,
    ) -> c_double;
    pub fn fastslide_slide_reader_get_best_level_for_downsample(
        reader: *const FastSlideSlideReader,
        downsample: c_double,
    ) -> c_int;

    // ---- slide_reader.h: properties / format ----
    pub fn fastslide_slide_reader_get_properties(
        reader: *const FastSlideSlideReader,
        properties: *mut FastSlideSlideProperties,
    ) -> c_int;
    pub fn fastslide_slide_reader_free_properties(properties: *mut FastSlideSlideProperties);
    pub fn fastslide_slide_reader_get_bounds(
        reader: *const FastSlideSlideReader,
        bounds: *mut FastSlideBounds,
    ) -> c_int;
    pub fn fastslide_slide_reader_get_format_name(
        reader: *const FastSlideSlideReader,
    ) -> *const c_char;
    pub fn fastslide_slide_reader_get_image_format(
        reader: *const FastSlideSlideReader,
    ) -> FastSlideImageFormat;
    pub fn fastslide_slide_reader_get_data_type(
        reader: *const FastSlideSlideReader,
    ) -> FastSlideDataType;
    pub fn fastslide_slide_reader_get_tile_size(
        reader: *const FastSlideSlideReader,
        tile_size: *mut FastSlideImageDimensions,
    ) -> c_int;

    // ---- slide_reader.h: channel metadata ----
    pub fn fastslide_slide_reader_get_channel_metadata(
        reader: *const FastSlideSlideReader,
        metadata: *mut *mut FastSlideChannelMetadata,
        num_channels: *mut c_int,
    ) -> c_int;
    pub fn fastslide_slide_reader_free_channel_metadata(
        metadata: *mut FastSlideChannelMetadata,
        num_channels: c_int,
    );

    // ---- slide_reader.h: region reading ----
    pub fn fastslide_slide_reader_read_region(
        reader: *const FastSlideSlideReader,
        region: *const FastSlideRegionSpec,
    ) -> *mut FastSlideImage;
    pub fn fastslide_slide_reader_read_region_coords(
        reader: *const FastSlideSlideReader,
        x: u32,
        y: u32,
        width: u32,
        height: u32,
        level: c_int,
        z: u32,
        t: u32,
    ) -> *mut FastSlideImage;
    pub fn fastslide_slide_reader_get_stack_info(
        reader: *const FastSlideSlideReader,
        info: *mut FastSlideStackInfo,
    ) -> c_int;

    // ---- slide_reader.h: multi-image container ----
    pub fn fastslide_slide_reader_get_image_count(reader: *const FastSlideSlideReader) -> c_int;
    pub fn fastslide_slide_reader_get_primary_image_index(
        reader: *const FastSlideSlideReader,
    ) -> c_int;
    pub fn fastslide_slide_reader_get_image_names(
        reader: *const FastSlideSlideReader,
        names: *mut *mut *mut c_char,
        num_names: *mut c_int,
    ) -> c_int;
    pub fn fastslide_slide_reader_free_image_names(names: *mut *mut c_char, num_names: c_int);
    pub fn fastslide_slide_reader_get_image(
        reader: *const FastSlideSlideReader,
        index: c_int,
    ) -> *mut FastSlideSlideImage;

    // ---- slide_reader.h: associated images ----
    pub fn fastslide_slide_reader_get_associated_image_names(
        reader: *const FastSlideSlideReader,
        names: *mut *mut *mut c_char,
        num_names: *mut c_int,
    ) -> c_int;
    pub fn fastslide_slide_reader_free_associated_image_names(
        names: *mut *mut c_char,
        num_names: c_int,
    );
    pub fn fastslide_slide_reader_get_associated_image_dimensions(
        reader: *const FastSlideSlideReader,
        name: *const c_char,
        dimensions: *mut FastSlideImageDimensions,
    ) -> c_int;
    pub fn fastslide_slide_reader_read_associated_image(
        reader: *const FastSlideSlideReader,
        name: *const c_char,
    ) -> *mut FastSlideImage;

    // ---- slide_reader.h: metadata access ----
    pub fn fastslide_slide_reader_get_metadata_keys(
        reader: *const FastSlideSlideReader,
        keys: *mut *mut *mut c_char,
        num_keys: *mut c_int,
    ) -> c_int;
    pub fn fastslide_slide_reader_free_metadata_keys(keys: *mut *mut c_char, num_keys: c_int);
    pub fn fastslide_slide_reader_get_metadata_value(
        reader: *const FastSlideSlideReader,
        key: *const c_char,
        value: *mut FastSlidePropertyValue,
    ) -> c_int;
    pub fn fastslide_slide_reader_get_metadata_type(
        reader: *const FastSlideSlideReader,
        key: *const c_char,
    ) -> FastSlidePropertyType;

    // ---- slide_reader.h: region validity / cleanup ----
    pub fn fastslide_slide_reader_is_region_valid(
        reader: *const FastSlideSlideReader,
        region: *const FastSlideRegionSpec,
    ) -> c_int;
    pub fn fastslide_slide_reader_clamp_region(
        reader: *const FastSlideSlideReader,
        region: *const FastSlideRegionSpec,
        clamped_region: *mut FastSlideRegionSpec,
    ) -> c_int;
    pub fn fastslide_slide_reader_free(reader: *mut FastSlideSlideReader);

    // ---- slide_reader.h: ICC color management ----
    pub fn fastslide_slide_reader_get_icc_profile_size(
        reader: *const FastSlideSlideReader,
    ) -> usize;
    pub fn fastslide_slide_reader_read_icc_profile(
        reader: *const FastSlideSlideReader,
        buffer: *mut u8,
        buffer_size: usize,
    ) -> usize;
    pub fn fastslide_slide_reader_enable_icc_transform(
        reader: *mut FastSlideSlideReader,
        target_space: FastSlideColorSpace,
        intent: FastSlideRenderingIntent,
        use_lut: c_int,
    ) -> c_int;

    // ---- slide_image.h: per-image (per-series) API ----
    pub fn fastslide_slide_image_free(image: *mut FastSlideSlideImage);
    pub fn fastslide_slide_image_get_level_count(image: *const FastSlideSlideImage) -> c_int;
    pub fn fastslide_slide_image_get_level_info(
        image: *const FastSlideSlideImage,
        level: c_int,
        info: *mut FastSlideLevelInfo,
    ) -> c_int;
    pub fn fastslide_slide_image_get_level_dimensions(
        image: *const FastSlideSlideImage,
        level: c_int,
        dimensions: *mut FastSlideImageDimensions,
    ) -> c_int;
    pub fn fastslide_slide_image_get_level_downsample(
        image: *const FastSlideSlideImage,
        level: c_int,
    ) -> c_double;
    pub fn fastslide_slide_image_get_base_dimensions(
        image: *const FastSlideSlideImage,
        dimensions: *mut FastSlideImageDimensions,
    ) -> c_int;
    pub fn fastslide_slide_image_get_tile_size(
        image: *const FastSlideSlideImage,
        tile_size: *mut FastSlideImageDimensions,
    ) -> c_int;
    pub fn fastslide_slide_image_get_image_format(
        image: *const FastSlideSlideImage,
    ) -> FastSlideImageFormat;
    pub fn fastslide_slide_image_get_data_type(
        image: *const FastSlideSlideImage,
    ) -> FastSlideDataType;
    pub fn fastslide_slide_image_get_channel_metadata(
        image: *const FastSlideSlideImage,
        metadata: *mut *mut FastSlideChannelMetadata,
        num_channels: *mut c_int,
    ) -> c_int;
    pub fn fastslide_slide_image_get_properties(
        image: *const FastSlideSlideImage,
        properties: *mut FastSlideSlideProperties,
    ) -> c_int;
    pub fn fastslide_slide_image_get_bounds(
        image: *const FastSlideSlideImage,
        bounds: *mut FastSlideBounds,
    ) -> c_int;
    pub fn fastslide_slide_image_get_stack_info(
        image: *const FastSlideSlideImage,
        info: *mut FastSlideStackInfo,
    ) -> c_int;
    pub fn fastslide_slide_image_read_region_coords(
        image: *const FastSlideSlideImage,
        x: u32,
        y: u32,
        width: u32,
        height: u32,
        level: c_int,
        z: u32,
        t: u32,
    ) -> *mut FastSlideImage;
    pub fn fastslide_slide_image_read_region(
        image: *const FastSlideSlideImage,
        region: *const FastSlideRegionSpec,
    ) -> *mut FastSlideImage;

    // ---- image.h: factory functions ----
    pub fn fastslide_image_create_rgb(
        dimensions: FastSlideImageDimensions,
        data_type: FastSlideDataType,
    ) -> *mut FastSlideImage;
    pub fn fastslide_image_create_rgba(
        dimensions: FastSlideImageDimensions,
        data_type: FastSlideDataType,
    ) -> *mut FastSlideImage;
    pub fn fastslide_image_create_grayscale(
        dimensions: FastSlideImageDimensions,
        data_type: FastSlideDataType,
    ) -> *mut FastSlideImage;
    pub fn fastslide_image_create_spectral(
        dimensions: FastSlideImageDimensions,
        channels: u32,
        data_type: FastSlideDataType,
    ) -> *mut FastSlideImage;
    pub fn fastslide_image_create_blank(
        dimensions: FastSlideImageDimensions,
    ) -> *mut FastSlideImage;
    pub fn fastslide_image_create_solid_color(
        dimensions: FastSlideImageDimensions,
        data_type: FastSlideDataType,
        red: u32,
        green: u32,
        blue: u32,
    ) -> *mut FastSlideImage;

    // ---- image.h: property accessors ----
    pub fn fastslide_image_get_info(
        image: *const FastSlideImage,
        info: *mut FastSlideImageInfo,
    ) -> c_int;
    pub fn fastslide_image_get_dimensions(
        image: *const FastSlideImage,
        dimensions: *mut FastSlideImageDimensions,
    ) -> c_int;
    pub fn fastslide_image_get_width(image: *const FastSlideImage) -> u32;
    pub fn fastslide_image_get_height(image: *const FastSlideImage) -> u32;
    pub fn fastslide_image_get_channels(image: *const FastSlideImage) -> u32;
    pub fn fastslide_image_get_format(image: *const FastSlideImage) -> FastSlideImageFormat;
    pub fn fastslide_image_get_data_type(image: *const FastSlideImage) -> FastSlideDataType;
    pub fn fastslide_image_get_planar_config(image: *const FastSlideImage)
        -> FastSlidePlanarConfig;
    pub fn fastslide_image_get_bytes_per_sample(image: *const FastSlideImage) -> usize;
    pub fn fastslide_image_is_empty(image: *const FastSlideImage) -> c_int;
    pub fn fastslide_image_is_initialized(image: *const FastSlideImage) -> c_int;
    pub fn fastslide_image_get_size_bytes(image: *const FastSlideImage) -> usize;
    pub fn fastslide_image_get_pixel_count(image: *const FastSlideImage) -> usize;

    // ---- image.h: data access ----
    pub fn fastslide_image_get_data(image: *const FastSlideImage) -> *const u8;
    pub fn fastslide_image_get_data_mutable(image: *mut FastSlideImage) -> *mut u8;
    pub fn fastslide_image_copy_data(
        image: *const FastSlideImage,
        buffer: *mut u8,
        buffer_size: usize,
    ) -> c_int;

    // ---- image.h: conversions ----
    pub fn fastslide_image_to_rgb(image: *const FastSlideImage) -> *mut FastSlideImage;
    pub fn fastslide_image_to_grayscale(image: *const FastSlideImage) -> *mut FastSlideImage;
    pub fn fastslide_image_to_planar(image: *const FastSlideImage) -> *mut FastSlideImage;
    pub fn fastslide_image_to_interleaved(image: *const FastSlideImage) -> *mut FastSlideImage;
    pub fn fastslide_image_extract_channels(
        image: *const FastSlideImage,
        channel_indices: *const u32,
        num_channels: u32,
    ) -> *mut FastSlideImage;
    pub fn fastslide_image_clone(image: *const FastSlideImage) -> *mut FastSlideImage;

    // ---- image.h: operations ----
    pub fn fastslide_image_paste(
        dest_image: *mut FastSlideImage,
        source_image: *const FastSlideImage,
        dest_x: u32,
        dest_y: u32,
        source_x: u32,
        source_y: u32,
        source_width: u32,
        source_height: u32,
    ) -> c_int;
    pub fn fastslide_image_get_description(
        image: *const FastSlideImage,
        buffer: *mut c_char,
        buffer_size: usize,
    ) -> c_int;
    pub fn fastslide_image_free(image: *mut FastSlideImage);

    // ---- utilities.h: resampling ----
    pub fn fastslide_lanczos_resample(
        image: *const FastSlideImage,
        output_width: u32,
        output_height: u32,
    ) -> *mut FastSlideImage;
    pub fn fastslide_lanczos2_resample(
        image: *const FastSlideImage,
        output_width: u32,
        output_height: u32,
    ) -> *mut FastSlideImage;
    pub fn fastslide_cosine_resample(
        image: *const FastSlideImage,
        output_width: u32,
        output_height: u32,
    ) -> *mut FastSlideImage;
    pub fn fastslide_average_resample(
        image: *const FastSlideImage,
        factor: u32,
    ) -> *mut FastSlideImage;
    pub fn fastslide_average_2x2_resample(image: *const FastSlideImage) -> *mut FastSlideImage;
    pub fn fastslide_average_4x4_resample(image: *const FastSlideImage) -> *mut FastSlideImage;
    pub fn fastslide_average_8x8_resample(image: *const FastSlideImage) -> *mut FastSlideImage;

    // ---- utilities.h: PNG I/O (examples) ----
    pub fn fastslide_examples_save_as_png(
        image: *const FastSlideImage,
        filename: *const c_char,
    ) -> c_int;
    pub fn fastslide_examples_load_from_png(filename: *const c_char) -> *mut FastSlideImage;
}
