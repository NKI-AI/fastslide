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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_PLAN_BUILDER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_PLAN_BUILDER_H_

#include <span>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/olympusvsi/olympusvsi_ets.h"
#include "fastslide/readers/olympusvsi/olympusvsi_level_info.h"

namespace fastslide::formats::olympusvsi {

/// @brief Build a `TilePlan` for an Olympus VSI region request.
///
/// Each `TileReadOp` corresponds to one physical .ets tile that intersects
/// the requested region. Op-field encoding:
///   - `source_id`   = unused (single backing .ets file, always 0).
///   - `byte_offset` = file byte offset of the compressed tile payload.
///   - `byte_size`   = compressed tile payload length in bytes.
///   - `tile_coord`  = (x, y) of the tile in the level's tile grid.
///   - `transform`   = region intersection (source crop -> dest paint).
///
/// Tiles whose grid cell is not present in the level's tile map are
/// silently skipped; the canvas paints background there.
class OlympusVsiPlanBuilder {
 public:
  static aifocore::Result<core::TilePlan> BuildPlan(
      const core::TileRequest& request,
      std::span<const OlympusVsiLevelInfo> pyramid, const EtsHeader& ets);
};

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_PLAN_BUILDER_H_
