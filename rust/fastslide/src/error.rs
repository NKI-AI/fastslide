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

//! Error type for the FastSlide bindings.

use std::ffi::CStr;
use std::fmt;

use fastslide_sys as sys;

/// Result alias used throughout the crate.
pub type Result<T> = std::result::Result<T, Error>;

/// An error returned by a FastSlide operation.
///
/// Mirrors the C API's error model: a failing call is detected by a null
/// pointer or zero/`-1` return, and the human-readable reason is fetched from
/// `fastslide_get_last_error()`. `op` records the binding operation that
/// failed (for context); `message` is the underlying library message.
#[derive(Debug, Clone)]
pub struct Error {
    op: &'static str,
    message: String,
}

impl Error {
    /// Build an error with an explicit message.
    pub(crate) fn new(op: &'static str, message: impl Into<String>) -> Self {
        Self {
            op,
            message: message.into(),
        }
    }

    /// Build an error from the library's last-error message.
    pub(crate) fn last(op: &'static str) -> Self {
        Self::new(op, last_error_message())
    }

    /// The binding operation that failed (e.g. `"open"`, `"read_region"`).
    #[must_use]
    pub fn op(&self) -> &str {
        self.op
    }

    /// The underlying FastSlide error message.
    #[must_use]
    pub fn message(&self) -> &str {
        &self.message
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "fastslide {}: {}", self.op, self.message)
    }
}

impl std::error::Error for Error {}

/// Read and clear the most recent error message from the C library.
pub(crate) fn last_error_message() -> String {
    // SAFETY: `fastslide_get_last_error` returns either null or a pointer to a
    // NUL-terminated string owned by the library and valid until the next call.
    unsafe {
        let ptr = sys::fastslide_get_last_error();
        if ptr.is_null() {
            "unknown error".to_string()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    }
}
