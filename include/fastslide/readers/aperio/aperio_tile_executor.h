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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TILE_EXECUTOR_H_

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/aperio/aperio_exec_context.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

/// @brief Tile executor for Aperio SVS slides with thread-local buffer
/// optimization
///
/// Provides sequential tile reading and decoding for Aperio slides with
/// optimized memory management:
/// 1. Uses simpletiff for thread-safe TIFF access
/// 2. Reads and decodes JPEG-compressed tiles via simpletiff
/// 3. Writes decoded pixels to the output buffer (Canvas applies source ROI)
/// 4. Missing tiles (data loss) are painted as zero-filled full tiles
///
/// Thread-local buffers eliminate per-tile allocations and improve cache
/// locality, providing performance benefits in both sequential and parallel
/// contexts.
class AperioTileExecutor : public CachedTileExecutor<AperioTileExecutor> {
 public:
  /// @brief Execute a tile plan sequentially with thread-local buffer
  /// optimization
  /// @param plan Pre-computed tile plan from PrepareRequest
  /// @param context Aperio execution context for data access
  /// @param writer Tile writer for output buffer management
  /// @return Status indicating success or failure
  /// @note Continues processing even if individual tiles fail (logs warnings).
  /// @note Per-op TIFF page geometry is derived from `op.source_id` and the
  ///       read-only `TiffIndex`, so concurrent ReadRegion calls on the same
  ///       reader are safe (no shared mutable per-request metadata).
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const AperioExecContext& context,
                                      runtime::Canvas& writer);

  /// @brief Per-op TIFF access parameters resolved from the read-only TIFF
  /// index.
  struct TiffAccessParams {
    uint16_t page = 0;
    uint32_t tile_width = 0;
    uint32_t tile_height = 0;
    uint16_t samples_per_pixel = 0;
    bool is_tiled = false;
  };

  /// @brief Resolve per-op TIFF geometry from the read-only TIFF index.
  ///
  /// Each `TileReadOp` carries its own page in `source_id`. The remaining
  /// geometry fields (tile size, samples_per_pixel, storage type) are
  /// immutable properties of that page. Looking them up on demand keeps the
  /// executor stateless and concurrent-safe.
  ///
  /// Exposed for regression testing of the no-shared-state invariant.
  static aifocore::Result<TiffAccessParams> ResolveAccessParams(
      const core::TileReadOp& op, const AperioExecContext& context);

  friend class CachedTileExecutor<AperioTileExecutor>;

 private:
  /// @brief Execute a single tile operation (called sequentially)
  /// @param op Tile operation descriptor
  /// @param context Aperio execution context
  /// @param params Per-op TIFF access parameters (page + geometry)
  /// @param writer Tile writer for output
  /// @return Status indicating success or failure
  static aifocore::Status ExecuteTileOperation(const core::TileReadOp& op,
                                               const AperioExecContext& context,
                                               const TiffAccessParams& params,
                                               runtime::Canvas& writer,
                                               std::mutex& writer_mutex);

  /// @brief Create cache key for a tile
  static TileKey MakeCacheKey(const core::TileReadOp& op,
                              const AperioExecContext& context,
                              const TiffAccessParams& params);

  /// @brief Read and decode a single TIFF tile/strip (called on cache miss)
  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& op, const AperioExecContext& context,
      const TiffAccessParams& params);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TILE_EXECUTOR_H_
