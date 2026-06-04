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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_TILE_EXECUTOR_H_

#include <mutex>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/bif/bif_exec_context.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

/// @brief Tile executor for Roche VENTANA BIF slides.
///
/// Decodes each referenced physical TIFF tile (tiled JPEG/YCbCr) via simpletiff
/// and paints it onto the Canvas at its sub-pixel stitched position. Decoded
/// physical tiles are shared through the per-reader LRU cache, keyed on
/// `(filename, source_page, tiff_tile_index)` so the same tile serves multiple
/// sub-rect crops at higher pyramid levels without re-decoding.
class BifTileExecutor : public CachedTileExecutor<BifTileExecutor> {
 public:
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const BifExecContext& context,
                                      runtime::Canvas& writer);

  friend class CachedTileExecutor<BifTileExecutor>;

 private:
  // Paints a single sub-rect operation from an already-decoded physical tile.
  static aifocore::Status PaintOp(const core::TileReadOp& op,
                                  const DecodedTileData& decoded,
                                  runtime::Canvas& writer,
                                  std::mutex& writer_mutex);

  static TileKey MakeCacheKey(const core::TileReadOp& op,
                              const BifExecContext& context);

  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& op, const BifExecContext& context);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_TILE_EXECUTOR_H_
