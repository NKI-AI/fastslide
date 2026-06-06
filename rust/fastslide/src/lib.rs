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

//! Safe, idiomatic Rust bindings for the FastSlide whole-slide image library.
//!
//! The API mirrors the FastSlide C++ object model on top of the stable C API
//! (the [`fastslide_sys`] crate):
//!
//! - [`SlideReader`] is the file/container handle (analogous to
//!   `openslide_open`): open it with [`SlideReader::open`], query the pyramid,
//!   and read regions.
//! - [`SlideImage`] is one navigable pyramid (series / scene) inside a
//!   container that exposes several (e.g. Olympus VSI). Obtain handles via
//!   [`SlideReader::image`].
//! - [`Image`] is an owned, decoded pixel buffer returned by region reads.
//!
//! Errors are reported as [`Error`] (see [`Result`]); messages come from the
//! library's last-error channel. Handles free their C resources on drop.
//!
//! ```no_run
//! use fastslide::{RegionSpec, SlideReader};
//!
//! let reader = SlideReader::open("slide.svs")?;
//! let base = reader.base_dimensions()?;
//! println!("{} x {}", base.width, base.height);
//!
//! let region = reader.read_region(&RegionSpec::new(
//!     Default::default(),
//!     fastslide::Dimensions { width: 512, height: 512 },
//!     0,
//! ))?;
//! let pixels: &[u8] = region.data();
//! # Ok::<(), fastslide::Error>(())
//! ```

mod error;
mod image;
mod metadata;
mod reader;
mod registry;
mod slide_image;
mod util;

pub use error::{Error, Result};
pub use image::{Image, Sample};
pub use metadata::{
    Bounds, ChannelMetadata, ColorRgb, Coordinate, DataType, Dimensions, ImageFormat, LevelInfo,
    PlanarConfig, RegionSpec, SlideProperties, StackInfo,
};
pub use reader::SlideReader;
pub use registry::{c_api_version, is_supported, supported_extensions, version};
pub use slide_image::SlideImage;

#[cfg(test)]
mod tests {
    use super::*;
    use fastslide_sys as sys;

    #[test]
    fn image_format_round_trips() {
        for f in [
            sys::FastSlideImageFormat::Spectral,
            sys::FastSlideImageFormat::Gray,
            sys::FastSlideImageFormat::Rgb,
            sys::FastSlideImageFormat::Rgba,
        ] {
            let mapped: ImageFormat = f.into();
            let expected = match f {
                sys::FastSlideImageFormat::Spectral => ImageFormat::Spectral,
                sys::FastSlideImageFormat::Gray => ImageFormat::Gray,
                sys::FastSlideImageFormat::Rgb => ImageFormat::Rgb,
                sys::FastSlideImageFormat::Rgba => ImageFormat::Rgba,
            };
            assert_eq!(mapped, expected);
        }
    }

    #[test]
    fn data_type_sizes() {
        assert_eq!(DataType::UInt8.size_bytes(), 1);
        assert_eq!(DataType::UInt16.size_bytes(), 2);
        assert_eq!(DataType::Int16.size_bytes(), 2);
        assert_eq!(DataType::Float32.size_bytes(), 4);
        assert_eq!(DataType::Float64.size_bytes(), 8);
    }

    #[test]
    fn region_spec_defaults_planes_to_zero() {
        let region = RegionSpec::new(
            Coordinate { x: 10, y: 20 },
            Dimensions {
                width: 256,
                height: 256,
            },
            2,
        );
        assert_eq!(region.z, 0);
        assert_eq!(region.t, 0);
        assert_eq!(region.level, 2);

        let sys_region = region.to_sys();
        assert_eq!(sys_region.top_left.x, 10);
        assert_eq!(sys_region.size.height, 256);
        assert_eq!(sys_region.level, 2);
    }

    #[test]
    fn stack_info_clamps_counts() {
        let info: StackInfo = sys::FastSlideStackInfo {
            z_count: 0,
            t_count: 0,
            has_z_spacing: 1,
            z_spacing_um: 0.5,
            has_t_interval: 0,
            t_interval_s: 0.0,
        }
        .into();
        assert_eq!(info.z_count, 1);
        assert_eq!(info.t_count, 1);
        assert_eq!(info.z_spacing_um, Some(0.5));
        assert_eq!(info.t_interval_s, None);
    }
}
