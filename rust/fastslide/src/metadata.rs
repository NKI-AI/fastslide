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

//! Plain-data value types mirroring the FastSlide C++/C metadata structs.

use std::os::raw::c_int;

use fastslide_sys as sys;

use crate::util::{cstr_to_string, optional_cstr};

/// Image dimensions in pixels, mirroring `fastslide::ImageDimensions`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Dimensions {
    pub width: u32,
    pub height: u32,
}

impl From<sys::FastSlideImageDimensions> for Dimensions {
    fn from(d: sys::FastSlideImageDimensions) -> Self {
        Self {
            width: d.width,
            height: d.height,
        }
    }
}

/// A pixel coordinate, mirroring `fastslide::ImageCoordinate`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Coordinate {
    pub x: u32,
    pub y: u32,
}

impl From<sys::FastSlideImageCoordinate> for Coordinate {
    fn from(c: sys::FastSlideImageCoordinate) -> Self {
        Self { x: c.x, y: c.y }
    }
}

/// Dimensions and downsample factor of a single pyramid level.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct LevelInfo {
    pub dimensions: Dimensions,
    pub downsample_factor: f64,
}

impl From<sys::FastSlideLevelInfo> for LevelInfo {
    fn from(info: sys::FastSlideLevelInfo) -> Self {
        Self {
            dimensions: info.dimensions.into(),
            downsample_factor: info.downsample_factor,
        }
    }
}

/// A rectangular region request, mirroring the C++ `RegionSpec`.
///
/// Coordinates are level-native (the same convention as OpenSlide
/// `read_region`). `z` and `t` select the focal plane and time point and
/// default to `0`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct RegionSpec {
    pub top_left: Coordinate,
    pub size: Dimensions,
    pub level: i32,
    pub z: u32,
    pub t: u32,
}

impl RegionSpec {
    /// Construct a region at plane `z = t = 0`.
    #[must_use]
    pub fn new(top_left: Coordinate, size: Dimensions, level: i32) -> Self {
        Self {
            top_left,
            size,
            level,
            z: 0,
            t: 0,
        }
    }

    pub(crate) fn to_sys(self) -> sys::FastSlideRegionSpec {
        sys::FastSlideRegionSpec {
            top_left: sys::FastSlideImageCoordinate {
                x: self.top_left.x,
                y: self.top_left.y,
            },
            size: sys::FastSlideImageDimensions {
                width: self.size.width,
                height: self.size.height,
            },
            level: self.level,
            z: self.z,
            t: self.t,
        }
    }
}

/// Physical slide properties, mirroring `fastslide::SlideProperties`.
#[derive(Debug, Clone, PartialEq)]
pub struct SlideProperties {
    pub mpp_x: f64,
    pub mpp_y: f64,
    pub objective_magnification: f64,
    pub objective_name: String,
    pub scanner_model: String,
    pub scan_date: Option<String>,
}

/// Bounding box of the non-empty slide region, in level-0 coordinates.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Bounds {
    pub x: i64,
    pub y: i64,
    pub width: i64,
    pub height: i64,
}

impl From<sys::FastSlideBounds> for Bounds {
    fn from(b: sys::FastSlideBounds) -> Self {
        Self {
            x: b.x,
            y: b.y,
            width: b.width,
            height: b.height,
        }
    }
}

/// An 8-bit RGB color.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ColorRgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl From<sys::FastSlideColorRGB> for ColorRgb {
    fn from(c: sys::FastSlideColorRGB) -> Self {
        Self {
            r: c.r,
            g: c.g,
            b: c.b,
        }
    }
}

/// Per-channel metadata, mirroring `fastslide::ChannelMetadata`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ChannelMetadata {
    pub name: String,
    pub biomarker: Option<String>,
    pub color: ColorRgb,
    pub exposure_time: u32,
    pub signal_units: u32,
}

/// Z (focal) / T (time) stack extent, mirroring `fastslide::StackInfo`.
///
/// `z_count` and `t_count` are always `>= 1`. The spacings are present only
/// when the underlying format reports them.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct StackInfo {
    pub z_count: u32,
    pub t_count: u32,
    pub z_spacing_um: Option<f64>,
    pub t_interval_s: Option<f64>,
}

impl Default for StackInfo {
    fn default() -> Self {
        Self {
            z_count: 1,
            t_count: 1,
            z_spacing_um: None,
            t_interval_s: None,
        }
    }
}

impl From<sys::FastSlideStackInfo> for StackInfo {
    fn from(info: sys::FastSlideStackInfo) -> Self {
        Self {
            z_count: info.z_count.max(1),
            t_count: info.t_count.max(1),
            z_spacing_um: (info.has_z_spacing != 0).then_some(info.z_spacing_um),
            t_interval_s: (info.has_t_interval != 0).then_some(info.t_interval_s),
        }
    }
}

/// Pixel layout of an image, mirroring `fastslide::ImageFormat`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImageFormat {
    /// N channels determined at runtime.
    Spectral,
    /// Single-channel grayscale.
    Gray,
    /// Three channels: red, green, blue.
    Rgb,
    /// Four channels: red, green, blue, alpha.
    Rgba,
}

impl From<sys::FastSlideImageFormat> for ImageFormat {
    fn from(f: sys::FastSlideImageFormat) -> Self {
        match f {
            sys::FastSlideImageFormat::Spectral => ImageFormat::Spectral,
            sys::FastSlideImageFormat::Gray => ImageFormat::Gray,
            sys::FastSlideImageFormat::Rgb => ImageFormat::Rgb,
            sys::FastSlideImageFormat::Rgba => ImageFormat::Rgba,
        }
    }
}

/// Pixel sample type, mirroring `fastslide::DataType`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DataType {
    UInt8,
    UInt16,
    Int16,
    UInt32,
    Int32,
    Float32,
    Float64,
}

impl From<sys::FastSlideDataType> for DataType {
    fn from(d: sys::FastSlideDataType) -> Self {
        match d {
            sys::FastSlideDataType::UInt8 => DataType::UInt8,
            sys::FastSlideDataType::UInt16 => DataType::UInt16,
            sys::FastSlideDataType::Int16 => DataType::Int16,
            sys::FastSlideDataType::UInt32 => DataType::UInt32,
            sys::FastSlideDataType::Int32 => DataType::Int32,
            sys::FastSlideDataType::Float32 => DataType::Float32,
            sys::FastSlideDataType::Float64 => DataType::Float64,
        }
    }
}

impl DataType {
    /// Size of a single sample of this type, in bytes.
    #[must_use]
    pub fn size_bytes(self) -> usize {
        match self {
            DataType::UInt8 => 1,
            DataType::UInt16 | DataType::Int16 => 2,
            DataType::UInt32 | DataType::Int32 | DataType::Float32 => 4,
            DataType::Float64 => 8,
        }
    }
}

/// Memory layout of multi-channel pixel data, mirroring
/// `fastslide::PlanarConfig`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlanarConfig {
    /// Interleaved (HWC): `RGBRGB...`.
    Contiguous,
    /// Planar (CHW): `RRR...GGG...BBB...`.
    Separate,
}

impl From<sys::FastSlidePlanarConfig> for PlanarConfig {
    fn from(p: sys::FastSlidePlanarConfig) -> Self {
        match p {
            sys::FastSlidePlanarConfig::Contiguous => PlanarConfig::Contiguous,
            sys::FastSlidePlanarConfig::Separate => PlanarConfig::Separate,
        }
    }
}

/// Copy a C-allocated channel-metadata array into a `Vec` and free it.
///
/// # Safety
/// `metadata` must be either null or a pointer to `count` valid entries
/// allocated by `fastslide_slide_*_get_channel_metadata`.
pub(crate) unsafe fn collect_channel_metadata(
    metadata: *mut sys::FastSlideChannelMetadata,
    count: c_int,
) -> Vec<ChannelMetadata> {
    if metadata.is_null() || count <= 0 {
        return Vec::new();
    }
    let channels = std::slice::from_raw_parts(metadata, count as usize)
        .iter()
        .map(|c| ChannelMetadata {
            name: cstr_to_string(c.name),
            biomarker: optional_cstr(c.biomarker),
            color: c.color.into(),
            exposure_time: c.exposure_time,
            signal_units: c.signal_units,
        })
        .collect();
    sys::fastslide_slide_reader_free_channel_metadata(metadata, count);
    channels
}

/// Copy a stack-allocated, library-filled properties struct into Rust and
/// release the strings the library allocated inside it.
///
/// # Safety
/// `props` must have been populated by a successful
/// `fastslide_slide_*_get_properties` call.
pub(crate) unsafe fn take_properties(mut props: sys::FastSlideSlideProperties) -> SlideProperties {
    let result = SlideProperties {
        mpp_x: props.mpp_x,
        mpp_y: props.mpp_y,
        objective_magnification: props.objective_magnification,
        objective_name: cstr_to_string(props.objective_name),
        scanner_model: cstr_to_string(props.scanner_model),
        scan_date: optional_cstr(props.scan_date),
    };
    sys::fastslide_slide_reader_free_properties(&mut props);
    result
}
