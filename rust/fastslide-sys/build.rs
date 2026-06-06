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

//! Cargo-only link helper.
//!
//! The canonical build path for this crate is Bazel (`rules_rs`), where the
//! FastSlide C library is linked via the `//:fastslide_c` dependency and this
//! script is not used. For plain `cargo` builds, point `FASTSLIDE_LIB_DIR` at a
//! directory containing the FastSlide shared/static library so the linker can
//! resolve the `fastslide_*` symbols.

fn main() {
    println!("cargo:rerun-if-env-changed=FASTSLIDE_LIB_DIR");
    if let Ok(lib_dir) = std::env::var("FASTSLIDE_LIB_DIR") {
        println!("cargo:rustc-link-search=native={lib_dir}");
        println!("cargo:rustc-link-lib=dylib=fastslide");
    }
}
