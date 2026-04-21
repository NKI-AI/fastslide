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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_TILE_EXECUTOR_H_

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/omezarr/omezarr.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

/// @brief Executor for OME-Zarr `TilePlan`s.
///
/// Reads compressed Zarr V3 chunks from disk, decodes them through the
/// per-level codec chain, extracts the requested channel slice, and paints
/// the slice onto the output canvas via `runtime::Canvas::PaintTilePlanar`.
class OmeZarrTileExecutor {
 public:
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const OmeZarrReader& reader,
                                      runtime::Canvas& canvas);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_TILE_EXECUTOR_H_
