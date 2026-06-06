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

//! Owned pixel buffer returned by region reads, mirroring `fastslide::Image`.

use fastslide_sys as sys;

use crate::error::{Error, Result};
use crate::metadata::{DataType, Dimensions, ImageFormat, PlanarConfig};

mod sealed {
    pub trait Sealed {}
}

/// A pixel sample type that can back an [`Image`] buffer.
///
/// Implemented for every [`DataType`] FastSlide supports. The associated
/// [`DataType`] is used to check, at runtime, that a typed view
/// ([`Image::samples`]) matches the image's actual element type.
pub trait Sample: sealed::Sealed + Copy {
    /// The [`DataType`] corresponding to this Rust type.
    const DATA_TYPE: DataType;
}

macro_rules! impl_sample {
    ($ty:ty, $dt:expr) => {
        impl sealed::Sealed for $ty {}
        impl Sample for $ty {
            const DATA_TYPE: DataType = $dt;
        }
    };
}

impl_sample!(u8, DataType::UInt8);
impl_sample!(u16, DataType::UInt16);
impl_sample!(i16, DataType::Int16);
impl_sample!(u32, DataType::UInt32);
impl_sample!(i32, DataType::Int32);
impl_sample!(f32, DataType::Float32);
impl_sample!(f64, DataType::Float64);

/// An owned, decoded image buffer.
///
/// This is the Rust counterpart of `fastslide::Image`: it owns the underlying
/// `FastSlideImage` handle and frees it on drop. Pixel data is exposed as raw
/// bytes ([`Image::data`]) or as a typed slice ([`Image::samples`]).
pub struct Image {
    ptr: *mut sys::FastSlideImage,
}

// SAFETY: `Image` exclusively owns its handle and the C library performs no
// hidden global mutation through it, so moving it across threads and sharing
// `&Image` (read-only accessors) between threads is sound.
unsafe impl Send for Image {}
unsafe impl Sync for Image {}

impl Image {
    /// Adopt a raw handle returned by the C API, erroring on null.
    ///
    /// # Safety
    /// `ptr` must be null or a handle whose ownership is transferred here
    /// (it will be freed on drop).
    pub(crate) unsafe fn from_raw(ptr: *mut sys::FastSlideImage, op: &'static str) -> Result<Self> {
        if ptr.is_null() {
            Err(Error::last(op))
        } else {
            Ok(Self { ptr })
        }
    }

    /// Image dimensions in pixels.
    #[must_use]
    pub fn dimensions(&self) -> Dimensions {
        Dimensions {
            width: self.width(),
            height: self.height(),
        }
    }

    /// Image width in pixels.
    #[must_use]
    pub fn width(&self) -> u32 {
        unsafe { sys::fastslide_image_get_width(self.ptr) }
    }

    /// Image height in pixels.
    #[must_use]
    pub fn height(&self) -> u32 {
        unsafe { sys::fastslide_image_get_height(self.ptr) }
    }

    /// Number of channels.
    #[must_use]
    pub fn channels(&self) -> u32 {
        unsafe { sys::fastslide_image_get_channels(self.ptr) }
    }

    /// Pixel format.
    #[must_use]
    pub fn format(&self) -> ImageFormat {
        unsafe { sys::fastslide_image_get_format(self.ptr) }.into()
    }

    /// Sample data type.
    #[must_use]
    pub fn data_type(&self) -> DataType {
        unsafe { sys::fastslide_image_get_data_type(self.ptr) }.into()
    }

    /// Channel memory layout (interleaved vs planar).
    #[must_use]
    pub fn planar_config(&self) -> PlanarConfig {
        unsafe { sys::fastslide_image_get_planar_config(self.ptr) }.into()
    }

    /// Total size of the pixel buffer in bytes.
    #[must_use]
    pub fn size_bytes(&self) -> usize {
        unsafe { sys::fastslide_image_get_size_bytes(self.ptr) }
    }

    /// Total number of pixels (`width * height`).
    #[must_use]
    pub fn pixel_count(&self) -> usize {
        unsafe { sys::fastslide_image_get_pixel_count(self.ptr) }
    }

    /// Whether the image holds no pixels.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        unsafe { sys::fastslide_image_is_empty(self.ptr) != 0 }
    }

    /// Raw pixel bytes. The length equals [`Image::size_bytes`].
    #[must_use]
    pub fn data(&self) -> &[u8] {
        let ptr = unsafe { sys::fastslide_image_get_data(self.ptr) };
        let len = self.size_bytes();
        if ptr.is_null() || len == 0 {
            return &[];
        }
        // SAFETY: the library guarantees `len` valid bytes behind `ptr` for as
        // long as `self` is alive; the borrow is tied to `&self`.
        unsafe { std::slice::from_raw_parts(ptr, len) }
    }

    /// Pixel data as a typed slice.
    ///
    /// Returns an error if `T` does not match the image's [`DataType`]. The
    /// returned length is the number of samples (`size_bytes / size_of::<T>()`).
    pub fn samples<T: Sample>(&self) -> Result<&[T]> {
        let actual = self.data_type();
        if actual != T::DATA_TYPE {
            return Err(Error::new(
                "samples",
                format!(
                    "requested {:?} samples but image is {:?}",
                    T::DATA_TYPE,
                    actual
                ),
            ));
        }
        let ptr = unsafe { sys::fastslide_image_get_data(self.ptr) };
        let len = self.size_bytes() / std::mem::size_of::<T>();
        if ptr.is_null() || len == 0 {
            return Ok(&[]);
        }
        // SAFETY: the element type matches `data_type()` (checked above), so the
        // buffer is correctly aligned and sized for `len` values of `T`.
        Ok(unsafe { std::slice::from_raw_parts(ptr.cast::<T>(), len) })
    }

    /// Convert to interleaved 8-bit RGB.
    pub fn to_rgb(&self) -> Result<Image> {
        unsafe { Image::from_raw(sys::fastslide_image_to_rgb(self.ptr), "to_rgb") }
    }

    /// Convert to single-channel grayscale.
    pub fn to_grayscale(&self) -> Result<Image> {
        unsafe { Image::from_raw(sys::fastslide_image_to_grayscale(self.ptr), "to_grayscale") }
    }

    /// Convert to planar (separate) layout: `RRR...GGG...BBB...`.
    pub fn to_planar(&self) -> Result<Image> {
        unsafe { Image::from_raw(sys::fastslide_image_to_planar(self.ptr), "to_planar") }
    }

    /// Convert to interleaved (contiguous) layout: `RGBRGB...`.
    pub fn to_interleaved(&self) -> Result<Image> {
        unsafe {
            Image::from_raw(
                sys::fastslide_image_to_interleaved(self.ptr),
                "to_interleaved",
            )
        }
    }
}

impl Drop for Image {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::fastslide_image_free(self.ptr) };
            self.ptr = std::ptr::null_mut();
        }
    }
}
