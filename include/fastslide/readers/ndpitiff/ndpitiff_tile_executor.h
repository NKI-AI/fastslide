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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_TILE_EXECUTOR_H_

#include <mutex>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/ndpitiff/ndpitiff_exec_context.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

/// @brief Tile executor for Hamamatsu NDPI slides.
///
/// Inherits from `CachedTileExecutor` so decoded tiles share the per-reader
/// LRU tile cache (matching Aperio/MRXS/CZI/iSyntax/DICOM behaviour). The
/// custom NDPI logic (headerless JPEG template + SOF dimension patching) lives
/// in `ReadTileFromDisk`, which is invoked only on cache misses.
class NdpiTiffTileExecutor : public CachedTileExecutor<NdpiTiffTileExecutor> {
 public:
  /// @brief Execute a tile plan in parallel with shared LRU caching.
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const NdpiTiffExecContext& context,
                                      runtime::Canvas& writer);

  friend class CachedTileExecutor<NdpiTiffTileExecutor>;

 private:
  /// @brief Execute a single tile operation (called once per parallel worker).
  static aifocore::Status ExecuteTileOperation(
      const core::TileReadOp& op, const NdpiTiffExecContext& context,
      runtime::Canvas& writer, std::mutex& writer_mutex);

  /// @brief Build a cache key for `op` (CRTP hook).
  ///
  /// `(filename, level, tile_x, tile_y)` is unique across both the tiled and
  /// strip storage paths since the plan builder sets `tile_coord.x = 0` for
  /// strip ops and `tile_coord.y = strip_index`.
  static TileKey MakeCacheKey(const core::TileReadOp& op,
                              const NdpiTiffExecContext& context);

  /// @brief Read+decode a single tile/strip on cache miss (CRTP hook).
  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& op, const NdpiTiffExecContext& context);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_TILE_EXECUTOR_H_
