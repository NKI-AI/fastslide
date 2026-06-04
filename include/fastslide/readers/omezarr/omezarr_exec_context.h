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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_EXEC_CONTEXT_H_

#include <memory>
#include <span>
#include <string_view>

#include "fastslide/readers/omezarr/omezarr_level_info.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide {

/// @brief Read-only view into the OME-Zarr reader state needed by the tile
/// executor.
///
/// The executor used to take `const OmeZarrReader&` and call
/// `reader.GetPyramid()`, `reader.GetCache()`, and `reader.GetFilename()`,
/// which created a call-graph cycle between `omezarr.cpp` and
/// `omezarr_tile_executor.cpp`. The reader now constructs an
/// `OmeZarrExecContext` once per `ExecutePlan` invocation and hands it to the
/// executor so the dependency runs strictly one-way.
struct OmeZarrExecContext {
  /// @brief Pyramid level descriptors, owned by the reader. Span is valid for
  /// the duration of the executor call.
  std::span<const OmeZarrLevelInfo> pyramid;

  /// @brief Optional decoded-chunk cache (may be null when caching is off).
  std::shared_ptr<runtime::ITileCache> cache;

  /// @brief Slide source path, used only as the cache-key namespace string.
  std::string_view filename;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_EXEC_CONTEXT_H_
