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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_PLAN_BUILDER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_PLAN_BUILDER_H_

#include <cstddef>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/omezarr/omezarr_level_info.h"

namespace fastslide {

/// @brief Builds a `TilePlan` for an OME-Zarr request.
///
/// Emits ONE `TileReadOp` per unique on-disk chunk (chunk_c, chunk_y,
/// chunk_x) that intersects the requested region. The executor decodes each
/// chunk once and slices it into every requested output channel that lives
/// in that chunk, so multi-channel chunks (`chunk_c > 1`) are not redundantly
/// decompressed once per channel.
///
/// Op-field encoding:
///   - `source_id`     = channel-chunk index (`source_channel / chunk_c`).
///   - `byte_offset`   = `(chunk_y << 32) | chunk_x` (spatial chunk coords).
///   - `byte_size`     = decompressed bytes of one on-disk chunk.
///   - `tile_coord.x`  = unused; the executor synthesizes per-output-channel
///                       paint ops with this field set to the canvas slot.
///   - `tile_coord.y`  = chunk_x (also used for cache-key uniqueness).
///   - `transform`     = chunk-region intersection (shared across channels).
class OmeZarrPlanBuilder {
 public:
  static aifocore::Result<core::TilePlan> BuildPlan(
      const core::TileRequest& request,
      const std::vector<OmeZarrLevelInfo>& pyramid,
      PlanarConfig output_planar_config, DataType data_type,
      const std::vector<size_t>& visible_channels);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_PLAN_BUILDER_H_
