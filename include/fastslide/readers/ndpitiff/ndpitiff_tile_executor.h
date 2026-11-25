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
/// in `DecodeTile`, which is invoked only on cache misses.
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
  /// `(filename, page, tile_x, tile_y)` is unique across the tiled and strip
  /// storage paths (the plan builder sets `tile_coord.x = 0` and
  /// `tile_coord.y = strip_index` for strips) and, crucially, across focal (Z)
  /// planes: NDPI z-stacks reuse the same (level, tile) grid per plane, so the
  /// TIFF page index (`source_id`) is the disambiguator.
  static TileKey MakeCacheKey(const core::TileReadOp& op,
                              const NdpiTiffExecContext& context);

  /// @brief Columns of a JPEG strip to decode instead of the whole strip.
  struct ColumnCrop {
    /// First requested column.
    uint32_t x = 0;
    /// Number of requested columns.
    uint32_t width = 0;
    /// Set to the first decoded column (libjpeg widens the range to iMCU
    /// boundaries).
    uint32_t decoded_x = 0;
  };

  /// @brief Read and decode one tile or strip into a tile that owns its pixels.
  /// @param crop If set, decode only these columns of a headerless NDPI JPEG
  ///        strip. The result is then not the whole strip and must not be
  ///        cached. Other storage ignores it and leaves `decoded_x` at 0.
  static aifocore::Result<CachedTileData> DecodeTile(
      const core::TileReadOp& op, const NdpiTiffExecContext& context,
      ColumnCrop* crop = nullptr);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_TILE_EXECUTOR_H_
