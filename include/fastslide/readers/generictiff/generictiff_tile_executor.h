// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_TILE_EXECUTOR_H_

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/generictiff/generictiff_exec_context.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

class GenericTiffTileExecutor
    : public CachedTileExecutor<GenericTiffTileExecutor> {
 public:
  /// @brief Execute a pre-built tile plan by reading and decoding TIFF tiles.
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const GenericTiffExecContext& context,
                                      runtime::Canvas& writer);

  /// @brief Cache key of a stored tile or strip: (page, tile/strip index).
  static runtime::TileKey MakeCacheKey(const core::TileReadOp& operation,
                                       const GenericTiffExecContext& context);

  /// @brief Decode a stored tile or strip into the thread-local buffer.
  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& operation, const GenericTiffExecContext& context);
};
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_TILE_EXECUTOR_H_
