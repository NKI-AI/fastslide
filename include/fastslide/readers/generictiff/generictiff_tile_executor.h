// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_TILE_EXECUTOR_H_

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/generictiff/generictiff_exec_context.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

class GenericTiffTileExecutor
    : public TiffBasedTileExecutor<GenericTiffTileExecutor> {
 public:
  /// @brief Execute a pre-built tile plan by reading and decoding TIFF tiles.
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const GenericTiffExecContext& context,
                                      runtime::Canvas& writer);
};
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_TILE_EXECUTOR_H_
