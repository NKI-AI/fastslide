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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_PIPELINE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_PIPELINE_H_

#include <filesystem>
#include <memory>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/slide_reader.h"

/// @file mrxs_pipeline.h
/// @brief Two-stage tile pipeline facade for the MRXS reader.
///
/// Hides the (`MrxsPlanBuilder` + `MrxsPlanContext`) and (`MrxsTileExecutor`
/// + `MrxsExecContext`) helper pairs behind a single small surface. Callers
/// supply raw slide state and the facade constructs the appropriate context
/// internally - cutting `mrxs.cpp`'s include fanout from four headers down
/// to one without changing runtime behaviour.

namespace fs = std::filesystem;

namespace fastslide {

namespace mrxs {

class MrxsSpatialIndex;

/// @brief Static facade combining MRXS plan building and tile execution.
class MrxsPipeline {
 public:
  /// @brief Build a tile plan for @p request.
  ///
  /// Equivalent to `MrxsPlanBuilder::BuildPlan` with the planning context
  /// constructed internally from the supplied slide state.
  ///
  /// @param request Tile request to plan for.
  /// @param slide_info Parsed slide metadata.
  /// @param level_info Level info for `request.level` (typically obtained
  ///        from `SlideReader::GetLevelInfo`).
  /// @param spatial_index Pre-built spatial index for the requested level.
  /// @return Tile plan or error.
  [[nodiscard]] static aifocore::Result<core::TilePlan> BuildPlan(
      const core::TileRequest& request, const SlideDataInfo& slide_info,
      const LevelInfo& level_info,
      std::shared_ptr<const MrxsSpatialIndex> spatial_index);

  /// @brief Execute a previously-built tile plan onto @p writer.
  ///
  /// Equivalent to `MrxsTileExecutor::ExecutePlan` with the execution
  /// context constructed internally.
  ///
  /// @param plan Tile plan to execute (from @ref BuildPlan).
  /// @param dirname MRXS directory used to resolve data files.
  /// @param slide_info Parsed slide metadata.
  /// @param cache Optional shared tile cache (may be null).
  /// @param writer Output canvas to paint into.
  [[nodiscard]] static aifocore::Status ExecutePlan(
      const core::TilePlan& plan, const fs::path& dirname,
      const SlideDataInfo& slide_info,
      std::shared_ptr<runtime::ITileCache> cache, runtime::Canvas& writer);
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_PIPELINE_H_
