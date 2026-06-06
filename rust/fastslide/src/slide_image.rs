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

//! A single navigable image (series / scene), mirroring `fastslide::SlideImage`.

use std::ptr;
use std::sync::Arc;

use fastslide_sys as sys;

use crate::error::{Error, Result};
use crate::image::Image;
use crate::metadata::{
    collect_channel_metadata, take_properties, Bounds, ChannelMetadata, DataType, Dimensions,
    ImageFormat, LevelInfo, RegionSpec, SlideProperties, StackInfo,
};
use crate::reader::{empty_properties, empty_stack_info, ReaderHandle};

/// One navigable pyramid inside a slide file.
///
/// Obtained from [`crate::SlideReader::image`]. Each handle keeps the owning
/// reader alive (via a shared `Arc`), so it is `'static`, `Send + Sync`, and
/// safe to store in long-lived structures. Per-image names are reported by
/// [`crate::SlideReader::image_names`] (the C API exposes them on the reader).
pub struct SlideImage {
    // Keeps the owning reader alive while this image handle is in use.
    _reader: Arc<ReaderHandle>,
    ptr: *mut sys::FastSlideSlideImage,
}

// SAFETY: the underlying reader is documented thread-safe for concurrent reads
// and is kept alive by the retained `Arc`; the per-image handle is freed once.
unsafe impl Send for SlideImage {}
unsafe impl Sync for SlideImage {}

impl SlideImage {
    pub(crate) fn from_raw(reader: Arc<ReaderHandle>, ptr: *mut sys::FastSlideSlideImage) -> Self {
        Self {
            _reader: reader,
            ptr,
        }
    }

    fn ptr(&self) -> *const sys::FastSlideSlideImage {
        self.ptr
    }

    /// Number of pyramid levels.
    #[must_use]
    pub fn level_count(&self) -> i32 {
        unsafe { sys::fastslide_slide_image_get_level_count(self.ptr()) }
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
        let ok = unsafe { sys::fastslide_slide_image_get_level_info(self.ptr(), level, &mut info) };
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
            sys::fastslide_slide_image_get_level_dimensions(self.ptr(), level, &mut dims)
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
        let ok = unsafe { sys::fastslide_slide_image_get_base_dimensions(self.ptr(), &mut dims) };
        if ok == 0 {
            return Err(Error::last("base_dimensions"));
        }
        Ok(dims.into())
    }

    /// Downsample factor of a level relative to level 0.
    pub fn level_downsample(&self, level: i32) -> Result<f64> {
        let ds = unsafe { sys::fastslide_slide_image_get_level_downsample(self.ptr(), level) };
        if ds <= 0.0 {
            return Err(Error::last("level_downsample"));
        }
        Ok(ds)
    }

    /// Level that best matches a desired downsample factor.
    ///
    /// Computed from this image's level table (the C API exposes the lookup
    /// only on the reader, so it is reproduced here for per-image use).
    #[must_use]
    pub fn best_level_for_downsample(&self, downsample: f64) -> i32 {
        let count = self.level_count();
        let mut best = 0;
        for level in 0..count {
            match self.level_downsample(level) {
                Ok(ds) if ds <= downsample => best = level,
                _ => break,
            }
        }
        best
    }

    /// Native tile size, or `{0, 0}` if untiled.
    pub fn tile_size(&self) -> Result<Dimensions> {
        let mut dims = sys::FastSlideImageDimensions {
            width: 0,
            height: 0,
        };
        let ok = unsafe { sys::fastslide_slide_image_get_tile_size(self.ptr(), &mut dims) };
        if ok == 0 {
            return Err(Error::last("tile_size"));
        }
        Ok(dims.into())
    }

    /// Pixel format.
    #[must_use]
    pub fn image_format(&self) -> ImageFormat {
        unsafe { sys::fastslide_slide_image_get_image_format(self.ptr()) }.into()
    }

    /// Sample data type.
    #[must_use]
    pub fn data_type(&self) -> DataType {
        unsafe { sys::fastslide_slide_image_get_data_type(self.ptr()) }.into()
    }

    /// Channel metadata for this image.
    pub fn channel_metadata(&self) -> Result<Vec<ChannelMetadata>> {
        let mut raw: *mut sys::FastSlideChannelMetadata = ptr::null_mut();
        let mut count = 0;
        let ok = unsafe {
            sys::fastslide_slide_image_get_channel_metadata(self.ptr(), &mut raw, &mut count)
        };
        if ok == 0 {
            return Err(Error::last("channel_metadata"));
        }
        Ok(unsafe { collect_channel_metadata(raw, count) })
    }

    /// Physical properties (MPP, objective, scanner, ...).
    pub fn properties(&self) -> Result<SlideProperties> {
        let mut props = empty_properties();
        let ok = unsafe { sys::fastslide_slide_image_get_properties(self.ptr(), &mut props) };
        if ok == 0 {
            return Err(Error::last("properties"));
        }
        Ok(unsafe { take_properties(props) })
    }

    /// Bounding box of the non-empty region, in level-0 coordinates.
    pub fn bounds(&self) -> Result<Bounds> {
        let mut bounds = sys::FastSlideBounds {
            x: 0,
            y: 0,
            width: 0,
            height: 0,
        };
        let ok = unsafe { sys::fastslide_slide_image_get_bounds(self.ptr(), &mut bounds) };
        if ok == 0 {
            return Err(Error::last("bounds"));
        }
        Ok(bounds.into())
    }

    /// Z/T stack extent.
    pub fn stack_info(&self) -> Result<StackInfo> {
        let mut info = empty_stack_info();
        let ok = unsafe { sys::fastslide_slide_image_get_stack_info(self.ptr(), &mut info) };
        if ok == 0 {
            return Err(Error::last("stack_info"));
        }
        Ok(info.into())
    }

    /// Read a rectangular region from this image.
    pub fn read_region(&self, region: &RegionSpec) -> Result<Image> {
        let spec = region.to_sys();
        let img = unsafe { sys::fastslide_slide_image_read_region(self.ptr(), &spec) };
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
            sys::fastslide_slide_image_read_region_coords(
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
}

impl Drop for SlideImage {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::fastslide_slide_image_free(self.ptr) };
            self.ptr = ptr::null_mut();
        }
    }
}
