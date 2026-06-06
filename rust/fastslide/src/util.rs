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

//! Small internal helpers for converting between C and Rust string types.

use std::ffi::CStr;
use std::os::raw::{c_char, c_int};

/// Convert a borrowed C string pointer to an owned `String`. Null becomes empty.
pub(crate) fn cstr_to_string(ptr: *const c_char) -> String {
    if ptr.is_null() {
        return String::new();
    }
    // SAFETY: `ptr` is non-null and points to a NUL-terminated string owned by
    // the library; we only read from it.
    unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() }
}

/// Like [`cstr_to_string`] but maps null / empty strings to `None`.
pub(crate) fn optional_cstr(ptr: *const c_char) -> Option<String> {
    let value = cstr_to_string(ptr);
    if value.is_empty() {
        None
    } else {
        Some(value)
    }
}

/// Copy a C array of C strings into a `Vec<String>` without freeing it.
///
/// The caller remains responsible for releasing `array` with the matching
/// FastSlide free function.
///
/// # Safety
/// `array` must either be null or point to `count` valid C string pointers.
pub(crate) unsafe fn collect_strings(array: *mut *mut c_char, count: c_int) -> Vec<String> {
    if array.is_null() || count <= 0 {
        return Vec::new();
    }
    let mut out = Vec::with_capacity(count as usize);
    for i in 0..count as isize {
        out.push(cstr_to_string(*array.offset(i)));
    }
    out
}
