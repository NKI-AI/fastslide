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

//! Library-level helpers: initialization, version, and the format registry.

use std::ffi::CString;
use std::path::Path;
use std::ptr;
use std::sync::Once;

use fastslide_sys as sys;

use crate::error::{Error, Result};
use crate::reader::CacheStats;
use crate::util::{collect_strings, cstr_to_string};

static INIT: Once = Once::new();

/// Initialize the format registry exactly once.
pub(crate) fn ensure_initialized() {
    INIT.call_once(|| {
        // SAFETY: idempotent C initializer; guarded by `Once`.
        unsafe {
            sys::fastslide_registry_initialize();
        }
    });
}

/// The FastSlide library version string.
#[must_use]
pub fn version() -> String {
    ensure_initialized();
    cstr_to_string(unsafe { sys::fastslide_get_version() })
}

/// The FastSlide C API version string (`"major.minor.patch"`).
#[must_use]
pub fn c_api_version() -> String {
    cstr_to_string(unsafe { sys::fastslide_c_api_get_version() })
}

/// The file extensions supported by the registered format readers.
#[must_use]
pub fn supported_extensions() -> Vec<String> {
    ensure_initialized();
    let mut exts: *mut *mut std::os::raw::c_char = ptr::null_mut();
    let mut count = 0;
    let ok = unsafe { sys::fastslide_get_supported_extensions(&mut exts, &mut count) };
    if ok == 0 {
        return Vec::new();
    }
    let result = unsafe { collect_strings(exts, count) };
    unsafe { sys::fastslide_registry_free_extensions(exts, count) };
    result
}

/// Whether a file path appears to be a supported slide format.
#[must_use]
pub fn is_supported(path: impl AsRef<Path>) -> bool {
    ensure_initialized();
    let Ok(c_path) = CString::new(path.as_ref().to_string_lossy().as_bytes()) else {
        return false;
    };
    unsafe { sys::fastslide_is_supported(c_path.as_ptr()) != 0 }
}

/// Resize the process-wide global tile cache.
///
/// Replaces the global cache with a new LRU cache of the requested capacity,
/// dropping any currently cached tiles. Readers attached via
/// [`crate::SlideReader::use_global_cache`] share this cache.
pub fn set_global_cache_capacity(capacity_bytes: usize) -> Result<()> {
    let ok = unsafe { sys::fastslide_global_cache_set_capacity_bytes(capacity_bytes) };
    if ok == 0 {
        return Err(Error::last("set_global_cache_capacity"));
    }
    Ok(())
}

/// Statistics for the process-wide global tile cache.
#[must_use]
pub fn global_cache_stats() -> Option<CacheStats> {
    let mut stats = sys::FastSlideCacheStats {
        capacity_bytes: 0,
        size: 0,
        hits: 0,
        misses: 0,
        hit_ratio: 0.0,
        memory_usage_bytes: 0,
    };
    let ok = unsafe { sys::fastslide_global_cache_get_stats(&mut stats) };
    if ok == 0 {
        return None;
    }
    Some(stats.into())
}

/// Clear all tiles from the process-wide global tile cache.
pub fn clear_global_cache() {
    unsafe { sys::fastslide_global_cache_clear() };
}

/// Where the tile operations of each read run.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReadParallelism {
    /// Spread over the shared thread pool; the calling thread waits (default).
    /// Lowest latency for one read at a time.
    ThreadPool,
    /// Run on the calling thread. Best throughput when the caller already runs
    /// many reads concurrently: no pool hand-off, and the pool size no longer
    /// caps how many reads progress at once.
    CallingThread,
}

/// Set where the tile operations of each read run (process-wide).
///
/// Also settable at startup with the `FASTSLIDE_READ_PARALLELISM` environment
/// variable (`thread_pool` or `calling_thread`).
pub fn set_read_parallelism(mode: ReadParallelism) {
    let mode = match mode {
        ReadParallelism::ThreadPool => sys::FastSlideReadParallelism::ThreadPool,
        ReadParallelism::CallingThread => sys::FastSlideReadParallelism::CallingThread,
    };
    // Cannot fail: every `FastSlideReadParallelism` value is a known mode.
    unsafe { sys::fastslide_set_read_parallelism(mode) };
}

/// Current read parallelism mode.
#[must_use]
pub fn read_parallelism() -> ReadParallelism {
    match unsafe { sys::fastslide_get_read_parallelism() } {
        sys::FastSlideReadParallelism::ThreadPool => ReadParallelism::ThreadPool,
        sys::FastSlideReadParallelism::CallingThread => ReadParallelism::CallingThread,
    }
}

/// Resize the shared thread pool used in [`ReadParallelism::ThreadPool`] mode.
///
/// Defaults to `NUM_THREADS` or the number of hardware threads, which in a
/// container is the host's, not the CPU quota. `0` means hardware concurrency.
pub fn set_thread_pool_size(num_threads: usize) {
    unsafe { sys::fastslide_set_thread_pool_size(num_threads) };
}
