// Copyright 2025 Jonas Teuwen. All Rights Reserved.
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

//! Standalone Rust smoke test for the installed FastSlide Debian packages.
//!
//! This reproduces exactly what the bug reporter's Rust project needs: link a
//! Rust program against the shipped `libfastslide.so` and call its C API. It is
//! compiled OUTSIDE cargo with `rustc` directly (see package/Dockerfile) so it
//! needs no crate registry, network, or Cargo.toml -- only the runtime deb on
//! the linker search path. The link line mirrors what rust/fastslide-sys's
//! build.rs emits for a cargo consumer (`-L native=<dir> -l dylib=fastslide`),
//! so a passing run proves the reporter's path works end to end.

use std::os::raw::{c_char, c_int};

// Minimal hand-written FFI mirror of include/fastslide/c/{fastslide,registry}.h
// (integer-returning functions yield 1 on success, 0 on failure).
extern "C" {
    fn fastslide_initialize() -> c_int;
    fn fastslide_cleanup();
    fn fastslide_c_api_get_version() -> *const c_char;
    fn fastslide_get_version() -> *const c_char;
    fn fastslide_get_supported_extensions(
        extensions: *mut *mut *mut c_char,
        num_extensions: *mut c_int,
    ) -> c_int;
    fn fastslide_registry_free_extensions(extensions: *mut *mut c_char, num_extensions: c_int);
}

/// Convert a borrowed C string pointer to an owned `String` for printing.
unsafe fn c_str_to_string(ptr: *const c_char) -> String {
    if ptr.is_null() {
        return String::from("(null)");
    }
    std::ffi::CStr::from_ptr(ptr).to_string_lossy().into_owned()
}

fn main() {
    unsafe {
        if fastslide_initialize() != 1 {
            eprintln!("ERROR: fastslide_initialize() failed");
            std::process::exit(1);
        }

        println!(
            "FastSlide C API version: {}",
            c_str_to_string(fastslide_c_api_get_version())
        );
        println!(
            "FastSlide library version: {}",
            c_str_to_string(fastslide_get_version())
        );

        let mut extensions: *mut *mut c_char = std::ptr::null_mut();
        let mut num_extensions: c_int = 0;
        if fastslide_get_supported_extensions(&mut extensions, &mut num_extensions) != 1 {
            eprintln!("ERROR: fastslide_get_supported_extensions() failed");
            fastslide_cleanup();
            std::process::exit(1);
        }

        println!("FastSlide supports {} file extension(s):", num_extensions);
        for i in 0..num_extensions as isize {
            let ext = *extensions.offset(i);
            println!("  - {}", c_str_to_string(ext));
        }

        let had_extensions = num_extensions > 0;
        fastslide_registry_free_extensions(extensions, num_extensions);

        if !had_extensions {
            eprintln!("ERROR: no extensions registered; the runtime package is broken.");
            fastslide_cleanup();
            std::process::exit(1);
        }

        fastslide_cleanup();
    }

    println!("FastSlide Debian package Rust smoke test passed.");
}
