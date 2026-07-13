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

//! The top-level slide handle, mirroring `fastslide::SlideReader`.

use std::ffi::CString;
use std::path::Path;
use std::ptr;
use std::sync::Arc;

use fastslide_sys as sys;

use crate::error::{Error, Result};
use crate::image::Image;
use crate::metadata::{
    collect_channel_metadata, take_properties, Bounds, ChannelMetadata, DataType, Dimensions,
    ImageFormat, LevelInfo, RegionSpec, SlideProperties, StackInfo,
};
use crate::registry::ensure_initialized;
use crate::slide_image::SlideImage;
use crate::util::collect_strings;

/// Shared owner of the C `FastSlideSlideReader`.
///
/// Held behind an `Arc` so per-image [`SlideImage`] handles can keep the
/// reader alive; the reader is freed once the last reference is dropped. The C
/// API documents per-image handles as borrowing the reader, so this `Arc`
/// enforces that lifetime at runtime while keeping both handles owned and
/// thread-safe.
pub(crate) struct ReaderHandle {
    pub(crate) ptr: *mut sys::FastSlideSlideReader,
}

// SAFETY: FastSlide readers are documented as thread-safe for concurrent
// region reads; the handle is only freed once, on the last `Arc` drop.
unsafe impl Send for ReaderHandle {}
unsafe impl Sync for ReaderHandle {}

impl Drop for ReaderHandle {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::fastslide_slide_reader_free(self.ptr) };
            self.ptr = ptr::null_mut();
        }
    }
}

/// Target color space for ICC color management.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ColorSpace {
    /// Standard RGB.
    Rgb,
    /// Linear-light RGB.
    Linear,
    /// sRGB (OpenSlide-compatible; the default target).
    Srgb,
    /// Determine automatically (defaults to sRGB).
    Automatic,
}

impl ColorSpace {
    fn to_sys(self) -> sys::FastSlideColorSpace {
        match self {
            ColorSpace::Rgb => sys::FastSlideColorSpace::Rgb,
            ColorSpace::Linear => sys::FastSlideColorSpace::Linear,
            ColorSpace::Srgb => sys::FastSlideColorSpace::Srgb,
            ColorSpace::Automatic => sys::FastSlideColorSpace::Automatic,
        }
    }
}

/// ICC rendering intent used when applying a color transform.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RenderingIntent {
    /// Perceptual (default, OpenSlide-compatible).
    Perceptual,
    /// Relative colorimetric.
    RelativeColorimetric,
    /// Saturation.
    Saturation,
    /// Absolute colorimetric.
    AbsoluteColorimetric,
}

impl RenderingIntent {
    fn to_sys(self) -> sys::FastSlideRenderingIntent {
        match self {
            RenderingIntent::Perceptual => sys::FastSlideRenderingIntent::Perceptual,
            RenderingIntent::RelativeColorimetric => {
                sys::FastSlideRenderingIntent::RelativeColorimetric
            }
            RenderingIntent::Saturation => sys::FastSlideRenderingIntent::Saturation,
            RenderingIntent::AbsoluteColorimetric => {
                sys::FastSlideRenderingIntent::AbsoluteColorimetric
            }
        }
    }
}

/// Options controlling how a slide is opened.
///
/// When `apply_icc` is set and the slide carries an embedded ICC profile,
/// [`SlideReader::read_region`] returns pixels already converted to
/// `target_color_space` using `rendering_intent`.
#[derive(Debug, Clone, Copy)]
pub struct OpenOptions {
    /// Apply the embedded ICC profile during region decode.
    pub apply_icc: bool,
    /// Target color space (sRGB by default).
    pub target_color_space: ColorSpace,
    /// Rendering intent.
    pub rendering_intent: RenderingIntent,
    /// Build the 256^3 8-bit LUT fast path (48 MiB, ~200 ms one-time cost) so
    /// 8-bit RGB(A) regions are color-managed with an O(1) table gather instead
    /// of an lcms2 pass. Ignored unless `apply_icc` is set.
    pub icc_use_lut: bool,
}

impl Default for OpenOptions {
    fn default() -> Self {
        Self {
            apply_icc: false,
            target_color_space: ColorSpace::Srgb,
            rendering_intent: RenderingIntent::Perceptual,
            icc_use_lut: false,
        }
    }
}

impl OpenOptions {
    fn to_sys(self) -> sys::FastSlideOpenOptions {
        sys::FastSlideOpenOptions {
            apply_icc: i32::from(self.apply_icc),
            target_color_space: self.target_color_space.to_sys(),
            rendering_intent: self.rendering_intent.to_sys(),
            icc_use_lut: i32::from(self.icc_use_lut),
        }
    }
}

/// A whole-slide image reader.
///
/// Open one with [`SlideReader::open`] (the analogue of the C++
/// `ReaderRegistry::CreateReader` and the Python `FastSlide.from_file_path`).
/// Cloning is cheap: clones share the same underlying file handle.
#[derive(Clone)]
pub struct SlideReader {
    inner: Arc<ReaderHandle>,
}

impl SlideReader {
    /// Open a slide file.
    ///
    /// Initializes the format registry on first use.
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        ensure_initialized();

        let path = path.as_ref();
        let c_path = CString::new(path.to_string_lossy().as_bytes())
            .map_err(|_| Error::new("open", "path contains an interior NUL byte"))?;

        // SAFETY: `c_path` is a valid NUL-terminated string for the call.
        let ptr = unsafe { sys::fastslide_create_reader(c_path.as_ptr()) };
        if ptr.is_null() {
            return Err(Error::last("open"));
        }
        Ok(Self {
            inner: Arc::new(ReaderHandle { ptr }),
        })
    }

    /// Open a slide file with explicit [`OpenOptions`] (e.g. ICC color
    /// management).
    ///
    /// Initializes the format registry on first use.
    pub fn open_with_options(path: impl AsRef<Path>, options: OpenOptions) -> Result<Self> {
        ensure_initialized();

        let path = path.as_ref();
        let c_path = CString::new(path.to_string_lossy().as_bytes()).map_err(|_| {
            Error::new("open_with_options", "path contains an interior NUL byte")
        })?;
        let sys_options = options.to_sys();

        // SAFETY: `c_path` is a valid NUL-terminated string and `sys_options`
        // outlives the call.
        let ptr = unsafe {
            sys::fastslide_create_reader_with_options(c_path.as_ptr(), &sys_options)
        };
        if ptr.is_null() {
            return Err(Error::last("open_with_options"));
        }
        Ok(Self {
            inner: Arc::new(ReaderHandle { ptr }),
        })
    }

    fn ptr(&self) -> *const sys::FastSlideSlideReader {
        self.inner.ptr
    }

    /// The slide's embedded ICC profile, or `None` if it has none.
    #[must_use]
    pub fn icc_profile(&self) -> Option<Vec<u8>> {
        let size = unsafe { sys::fastslide_slide_reader_get_icc_profile_size(self.ptr()) };
        if size == 0 {
            return None;
        }
        let mut buffer = vec![0u8; size];
        let written = unsafe {
            sys::fastslide_slide_reader_read_icc_profile(self.ptr(), buffer.as_mut_ptr(), size)
        };
        if written == 0 {
            return None;
        }
        buffer.truncate(written);
        Some(buffer)
    }

    /// Enable in-library ICC color management for subsequent reads.
    ///
    /// Builds a transform from the slide's embedded ICC profile to `target`
    /// and applies it in place during [`SlideReader::read_region`]. On a slide
    /// with no embedded profile this is a successful no-op (reads stay native).
    ///
    /// When `use_lut` is set, the 256^3 8-bit LUT fast path is built (48 MiB,
    /// ~200 ms one-time cost) so 8-bit RGB(A) regions are color-managed with an
    /// O(1) table gather instead of an lcms2 pass.
    pub fn enable_icc_transform(
        &self,
        target: ColorSpace,
        intent: RenderingIntent,
        use_lut: bool,
    ) -> Result<()> {
        let ok = unsafe {
            sys::fastslide_slide_reader_enable_icc_transform(
                self.inner.ptr,
                target.to_sys(),
                intent.to_sys(),
                i32::from(use_lut),
            )
        };
        if ok == 0 {
            return Err(Error::last("enable_icc_transform"));
        }
        Ok(())
    }

    /// Number of pyramid levels of the primary image.
    #[must_use]
    pub fn level_count(&self) -> i32 {
        unsafe { sys::fastslide_slide_reader_get_level_count(self.ptr()) }
    }

    /// Dimensions and downsample factor of a level.
    pub fn level_info(&self, level: i32) -> Result<LevelInfo> {
        let mut info = sys::FastSlideLevelInfo {
            dimensions: sys::FastSlideImageDimensions {
                width: 0,
                height: 0,
            },
            downsample_factor: 0.0,
        };
        let ok =
            unsafe { sys::fastslide_slide_reader_get_level_info(self.ptr(), level, &mut info) };
        if ok == 0 {
            return Err(Error::last("level_info"));
        }
        Ok(info.into())
    }

    /// Pixel dimensions of a level.
    pub fn level_dimensions(&self, level: i32) -> Result<Dimensions> {
        let mut dims = sys::FastSlideImageDimensions {
            width: 0,
            height: 0,
        };
        let ok = unsafe {
            sys::fastslide_slide_reader_get_level_dimensions(self.ptr(), level, &mut dims)
        };
        if ok == 0 {
            return Err(Error::last("level_dimensions"));
        }
        Ok(dims.into())
    }

    /// Pixel dimensions of level 0.
    pub fn base_dimensions(&self) -> Result<Dimensions> {
        let mut dims = sys::FastSlideImageDimensions {
            width: 0,
            height: 0,
        };
        let ok = unsafe { sys::fastslide_slide_reader_get_base_dimensions(self.ptr(), &mut dims) };
        if ok == 0 {
            return Err(Error::last("base_dimensions"));
        }
        Ok(dims.into())
    }

    /// Downsample factor of a level relative to level 0.
    pub fn level_downsample(&self, level: i32) -> Result<f64> {
        let ds = unsafe { sys::fastslide_slide_reader_get_level_downsample(self.ptr(), level) };
        if ds <= 0.0 {
            return Err(Error::last("level_downsample"));
        }
        Ok(ds)
    }

    /// Level that best matches a desired downsample factor.
    #[must_use]
    pub fn best_level_for_downsample(&self, downsample: f64) -> i32 {
        unsafe { sys::fastslide_slide_reader_get_best_level_for_downsample(self.ptr(), downsample) }
    }

    /// Physical slide properties (MPP, objective, scanner, ...).
    pub fn properties(&self) -> Result<SlideProperties> {
        let mut props = empty_properties();
        let ok = unsafe { sys::fastslide_slide_reader_get_properties(self.ptr(), &mut props) };
        if ok == 0 {
            return Err(Error::last("properties"));
        }
        Ok(unsafe { take_properties(props) })
    }

    /// Bounding box of the non-empty slide region, in level-0 coordinates.
    pub fn bounds(&self) -> Result<Bounds> {
        let mut bounds = sys::FastSlideBounds {
            x: 0,
            y: 0,
            width: 0,
            height: 0,
        };
        let ok = unsafe { sys::fastslide_slide_reader_get_bounds(self.ptr(), &mut bounds) };
        if ok == 0 {
            return Err(Error::last("bounds"));
        }
        Ok(bounds.into())
    }

    /// Human-readable format name (e.g. `"Aperio"`).
    #[must_use]
    pub fn format_name(&self) -> String {
        let ptr = unsafe { sys::fastslide_slide_reader_get_format_name(self.ptr()) };
        crate::util::cstr_to_string(ptr)
    }

    /// Pixel format of the primary image.
    #[must_use]
    pub fn image_format(&self) -> ImageFormat {
        unsafe { sys::fastslide_slide_reader_get_image_format(self.ptr()) }.into()
    }

    /// Sample data type of the primary image.
    #[must_use]
    pub fn data_type(&self) -> DataType {
        unsafe { sys::fastslide_slide_reader_get_data_type(self.ptr()) }.into()
    }

    /// Native tile size, or `{0, 0}` if the image is untiled.
    pub fn tile_size(&self) -> Result<Dimensions> {
        let mut dims = sys::FastSlideImageDimensions {
            width: 0,
            height: 0,
        };
        let ok = unsafe { sys::fastslide_slide_reader_get_tile_size(self.ptr(), &mut dims) };
        if ok == 0 {
            return Err(Error::last("tile_size"));
        }
        Ok(dims.into())
    }

    /// Channel metadata for the primary image.
    pub fn channel_metadata(&self) -> Result<Vec<ChannelMetadata>> {
        let mut raw: *mut sys::FastSlideChannelMetadata = ptr::null_mut();
        let mut count = 0;
        let ok = unsafe {
            sys::fastslide_slide_reader_get_channel_metadata(self.ptr(), &mut raw, &mut count)
        };
        if ok == 0 {
            return Err(Error::last("channel_metadata"));
        }
        Ok(unsafe { collect_channel_metadata(raw, count) })
    }

    /// Z/T stack extent of the primary image.
    pub fn stack_info(&self) -> Result<StackInfo> {
        let mut info = empty_stack_info();
        let ok = unsafe { sys::fastslide_slide_reader_get_stack_info(self.ptr(), &mut info) };
        if ok == 0 {
            return Err(Error::last("stack_info"));
        }
        Ok(info.into())
    }

    /// Read a rectangular region from the primary image.
    pub fn read_region(&self, region: &RegionSpec) -> Result<Image> {
        let spec = region.to_sys();
        let img = unsafe { sys::fastslide_slide_reader_read_region(self.ptr(), &spec) };
        unsafe { Image::from_raw(img, "read_region") }
    }

    /// Read a region using explicit coordinates (`z`/`t` default to `0`).
    pub fn read_region_coords(
        &self,
        x: u32,
        y: u32,
        width: u32,
        height: u32,
        level: i32,
        z: u32,
        t: u32,
    ) -> Result<Image> {
        let img = unsafe {
            sys::fastslide_slide_reader_read_region_coords(
                self.ptr(),
                x,
                y,
                width,
                height,
                level,
                z,
                t,
            )
        };
        unsafe { Image::from_raw(img, "read_region_coords") }
    }

    /// Number of navigable images (pyramids) in this file (always `>= 1`).
    #[must_use]
    pub fn image_count(&self) -> i32 {
        unsafe { sys::fastslide_slide_reader_get_image_count(self.ptr()) }
    }

    /// Index of the primary image.
    #[must_use]
    pub fn primary_image_index(&self) -> i32 {
        unsafe { sys::fastslide_slide_reader_get_primary_image_index(self.ptr()) }
    }

    /// Human-readable names for every image, in index order.
    #[must_use]
    pub fn image_names(&self) -> Vec<String> {
        let mut names: *mut *mut std::os::raw::c_char = ptr::null_mut();
        let mut count = 0;
        let ok = unsafe {
            sys::fastslide_slide_reader_get_image_names(self.ptr(), &mut names, &mut count)
        };
        if ok == 0 {
            return Vec::new();
        }
        let result = unsafe { collect_strings(names, count) };
        unsafe { sys::fastslide_slide_reader_free_image_names(names, count) };
        result
    }

    /// Get a handle to the `index`-th navigable image.
    pub fn image(&self, index: i32) -> Result<SlideImage> {
        let img = unsafe { sys::fastslide_slide_reader_get_image(self.ptr(), index) };
        if img.is_null() {
            return Err(Error::last("image"));
        }
        Ok(SlideImage::from_raw(Arc::clone(&self.inner), img))
    }

    /// Names of available associated images (label, macro, thumbnail, ...).
    #[must_use]
    pub fn associated_image_names(&self) -> Vec<String> {
        let mut names: *mut *mut std::os::raw::c_char = ptr::null_mut();
        let mut count = 0;
        let ok = unsafe {
            sys::fastslide_slide_reader_get_associated_image_names(
                self.ptr(),
                &mut names,
                &mut count,
            )
        };
        if ok == 0 {
            return Vec::new();
        }
        let result = unsafe { collect_strings(names, count) };
        unsafe { sys::fastslide_slide_reader_free_associated_image_names(names, count) };
        result
    }

    /// Dimensions of a named associated image.
    pub fn associated_image_dimensions(&self, name: &str) -> Result<Dimensions> {
        let c_name = CString::new(name)
            .map_err(|_| Error::new("associated_image_dimensions", "name contains a NUL byte"))?;
        let mut dims = sys::FastSlideImageDimensions {
            width: 0,
            height: 0,
        };
        let ok = unsafe {
            sys::fastslide_slide_reader_get_associated_image_dimensions(
                self.ptr(),
                c_name.as_ptr(),
                &mut dims,
            )
        };
        if ok == 0 {
            return Err(Error::last("associated_image_dimensions"));
        }
        Ok(dims.into())
    }

    /// Read a named associated image.
    pub fn read_associated_image(&self, name: &str) -> Result<Image> {
        let c_name = CString::new(name)
            .map_err(|_| Error::new("read_associated_image", "name contains a NUL byte"))?;
        let img = unsafe {
            sys::fastslide_slide_reader_read_associated_image(self.ptr(), c_name.as_ptr())
        };
        unsafe { Image::from_raw(img, "read_associated_image") }
    }
}

pub(crate) fn empty_properties() -> sys::FastSlideSlideProperties {
    sys::FastSlideSlideProperties {
        mpp_x: 0.0,
        mpp_y: 0.0,
        objective_magnification: 0.0,
        objective_name: ptr::null_mut(),
        scanner_model: ptr::null_mut(),
        scan_date: ptr::null_mut(),
    }
}

pub(crate) fn empty_stack_info() -> sys::FastSlideStackInfo {
    sys::FastSlideStackInfo {
        z_count: 1,
        t_count: 1,
        has_z_spacing: 0,
        z_spacing_um: 0.0,
        has_t_interval: 0,
        t_interval_s: 0.0,
    }
}
