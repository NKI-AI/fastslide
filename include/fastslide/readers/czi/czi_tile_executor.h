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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_TILE_EXECUTOR_H_

#include <cstdint>
#include <mutex>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/czi/czi_exec_context.h"

namespace fastslide {

namespace runtime {
class Canvas;
}  // namespace runtime

/// @brief Tile executor for CZI (two-stage pipeline stage 2).
class CziTileExecutor : public CachedTileExecutor<CziTileExecutor> {
 public:
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const CziExecContext& context,
                                      runtime::Canvas& writer);

  // CachedTileExecutor hooks.
  static runtime::TileKey MakeCacheKey(const core::TileReadOp& op,
                                       const CziExecContext& context);

  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& op, const CziExecContext& context);

 private:
  static aifocore::Status ExecuteTileOperation(const core::TileReadOp& op,
                                               const CziExecContext& context,
                                               runtime::Canvas& writer,
                                               std::mutex& writer_mutex);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_TILE_EXECUTOR_H_
