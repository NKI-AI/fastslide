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

#ifndef AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_TILE_EXECUTOR_H_

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/isyntax/isyntax_exec_context.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

/// @brief Helper class to execute iSyntax tile reading plans
class IsyntaxTileExecutor : public CachedTileExecutor<IsyntaxTileExecutor> {
 public:
  /// @brief Execute the plan
  /// @param plan The execution plan
  /// @param context The execution context (holds the libisyntax handle)
  /// @param writer The tile writer for output
  /// @return Status indicating success or failure
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const IsyntaxExecContext& context,
                                      runtime::Canvas& writer);

  friend class CachedTileExecutor<IsyntaxTileExecutor>;

 private:
  /// @brief Execute a single tile operation
  /// @param op The tile operation
  /// @param context The execution context
  /// @param writer The tile writer for output
  /// @param accumulator_mutex Mutex for thread-safe tile writing
  /// @return Status indicating success or failure
  static aifocore::Status ExecuteTileOperation(
      const core::TileReadOp& op, const IsyntaxExecContext& context,
      runtime::Canvas& writer, std::mutex& accumulator_mutex);

  /// @brief Create cache key for a tile
  /// @param op The tile operation
  /// @param context The execution context
  /// @return TileKey for cache lookup
  static runtime::TileKey MakeCacheKey(const core::TileReadOp& op,
                                       const IsyntaxExecContext& context);

  /// @brief Read and decode a single tile (called on cache miss)
  /// @param op The tile operation
  /// @param context The execution context
  /// @return Decoded RGB tile data or error status
  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& op, const IsyntaxExecContext& context);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_SRC_READERS_ISYNTAX_ISYNTAX_TILE_EXECUTOR_H_
