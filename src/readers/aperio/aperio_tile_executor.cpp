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

#include "fastslide/readers/aperio/aperio_tile_executor.h"

#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status AperioTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const AperioExecContext& context,
    runtime::Canvas& writer) {
  std::atomic<int> error_count{0};
  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolBestEffort(
      plan, writer,
      [&](const core::TileReadOp& operation, runtime::Canvas& writer_ref,
          std::mutex& writer_mutex) -> aifocore::Status {
        // Resolve the per-op TIFF geometry from the read-only TiffIndex on
        // every tile. This intentionally avoids any cross-request mutable
        // metadata cache, which previously caused page/tile_index mismatches
        // when ReadRegion was called concurrently for different levels.
        auto params_or = ResolveAccessParams(operation, context);
        if (!params_or.ok()) {
          return params_or.status();
        }
        return ExecuteTileOperation(operation, context, *params_or, writer_ref,
                                    writer_mutex);
      },
      [&](const core::TileReadOp& operation, const aifocore::Status& status) {
        const int count =
            error_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 10) {
          std::cerr << "Tile at (" << operation.tile_coord.x << ", "
                    << operation.tile_coord.y
                    << ") failed: " << status.ToString() << "\n";
        }
      });
}

aifocore::Result<AperioTileExecutor::TiffAccessParams>
AperioTileExecutor::ResolveAccessParams(const core::TileReadOp& op,
                                        const AperioExecContext& context) {
  const auto& tiff_index = context.GetTiffIndex();
  const uint32_t page_index = op.source_id;
  if (page_index >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("AperioTileExecutor: op.source_id={} out of "
                              "range (file has {} pages)",
                              page_index, tiff_index.NumPages()));
  }

  const auto& page = tiff_index.Page(page_index);
  TiffAccessParams params;
  params.page = static_cast<uint16_t>(page_index);
  params.samples_per_pixel = page.samples_per_pixel;
  params.is_tiled = (page.storage == simpletiff::Storage::kTiles);

  if (params.is_tiled) {
    const auto& tiles = tiff_index.Tiles(page.payload_id);
    params.tile_width = tiles.tile_w;
    params.tile_height = tiles.tile_h;
  } else if (page.storage == simpletiff::Storage::kStrips) {
    const auto& strips = tiff_index.Strips(page.payload_id);
    params.tile_width = page.width;
    params.tile_height =
        (strips.rows_per_strip == 0) ? page.height : strips.rows_per_strip;
  } else {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "AperioTileExecutor: page {} uses unsupported storage type {}",
            page_index, static_cast<int>(page.storage)));
  }

  return params;
}

aifocore::Status AperioTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& operation, const AperioExecContext& context,
    const TiffAccessParams& params, runtime::Canvas& writer,
    std::mutex& writer_mutex) {
  auto tile_or = ReadTile(operation, context, params);

  aifocore::Status write_status;
  if (tile_or.ok()) {
    const TileHandle& tile = *tile_or;
    write_status = readers::simpletiff_exec::PaintTileMaybeLocked(
        writer, operation, tile->data, tile->size[0], tile->size[1],
        tile->channels, writer_mutex);
  } else if (tile_or.status().code() == aifocore::StatusCode::kDataLoss) {
    // A tile with lost data reads as black.
    const std::vector<uint8_t> zeros(static_cast<size_t>(params.tile_width) *
                                     params.tile_height *
                                     params.samples_per_pixel);
    write_status = readers::simpletiff_exec::PaintTileMaybeLocked(
        writer, operation, zeros, params.tile_width, params.tile_height,
        static_cast<uint32_t>(params.samples_per_pixel), writer_mutex);
  } else {
    std::cerr << aifocore::fmt::format(
        "Failed to read/decode tile at ({}, {}) in {}: {}\n",
        operation.tile_coord.x, operation.tile_coord.y, context.GetFilename(),
        tile_or.status().ToString());
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  if (!write_status.ok()) {
    std::cerr << "Failed to write tile: " << write_status.ToString();
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  return aifocore::Status::OkStatus();
}

runtime::TileKey AperioTileExecutor::MakeCacheKey(
    const core::TileReadOp& operation, const AperioExecContext& context,
    const TiffAccessParams& params) {
  (void)params;
  // Use tile grid coordinates for key (unique per level)
  return runtime::TileKey(context.GetFilename(),
                          static_cast<uint16_t>(operation.level),
                          operation.tile_coord.x, operation.tile_coord.y);
}

aifocore::Result<CachedTileData> AperioTileExecutor::DecodeTile(
    const core::TileReadOp& operation, const AperioExecContext& context,
    const TiffAccessParams& params) {
  // Each worker thread uses its own DecodeContext for decompression. This is
  // thread-safe and avoids per-tile allocations.
  static thread_local simpletiff::DecodeContext decode_ctx;

  const auto& tiff_index = context.GetTiffIndex();
  // operation.byte_offset is the linear tile index for tiled TIFFs and the
  // strip index for striped TIFFs (e.g. some SVS associated images / fallback
  // pyramids). The shared helper picks the right code path based on the page
  // header.
  const uint32_t tile_or_strip_index =
      static_cast<uint32_t>(operation.byte_offset);

  AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                            readers::simpletiff_decode::DecodeTileOrStrip(
                                tiff_index, static_cast<uint32_t>(params.page),
                                tile_or_strip_index, decode_ctx));

  // For tiled storage we expect the decoded tile to match the planned tile
  // geometry. Strip storage produces strip-shaped buffers, so don't compare.
  if (params.is_tiled && (decoded.size[0] != params.tile_width ||
                          decoded.size[1] != params.tile_height)) {
    std::cerr << "Tile dimension mismatch: expected " << params.tile_width
              << "x" << params.tile_height << ", got " << decoded.size[0] << "x"
              << decoded.size[1] << "\n";
  }
  return decoded;
}

}  // namespace fastslide
