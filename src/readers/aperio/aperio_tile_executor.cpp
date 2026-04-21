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
#include "fastslide/readers/aperio/aperio.h"
#include "fastslide/readers/aperio/aperio_plan_builder.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status AperioTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const AperioReader& reader,
    runtime::Canvas& writer, const TiffStructureMetadata& tiff_metadata) {
  std::atomic<int> error_count{0};
  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolBestEffort(
      plan, writer,
      [&](const core::TileReadOp& operation, runtime::Canvas& writer_ref,
          std::mutex& writer_mutex) -> aifocore::Status {
        return ExecuteTileOperation(
            operation, reader, tiff_metadata.page, tiff_metadata.tile_width,
            tiff_metadata.tile_height, tiff_metadata.samples_per_pixel,
            tiff_metadata.is_tiled, writer_ref, writer_mutex);
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

aifocore::Status AperioTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& operation, const AperioReader& reader,
    uint16_t page, uint32_t tile_width, uint32_t tile_height,
    uint16_t samples_per_pixel, bool is_tiled, runtime::Canvas& writer,
    std::mutex& writer_mutex) {
  const TiffAccessParams tiff_params = {
      .page = page,
      .tile_width = tile_width,
      .tile_height = tile_height,
      .samples_per_pixel = samples_per_pixel,
      .is_tiled = is_tiled,
  };

  auto decoded_or = ReadWithCacheDecoded(operation, reader, tiff_params);

  std::span<const uint8_t> pixel_data;
  uint32_t paint_w = tile_width;
  uint32_t paint_h = tile_height;
  uint32_t paint_channels = static_cast<uint32_t>(samples_per_pixel);

  if (!decoded_or.ok()) {
    if (decoded_or.status().code() == aifocore::StatusCode::kDataLoss) {
      const size_t full_tile_bytes =
          static_cast<size_t>(tile_width) * tile_height * samples_per_pixel;
      uint8_t* zeros =
          TiffBasedTileExecutor<AperioTileExecutor>::GetBuffers().GetCropBuffer(
              full_tile_bytes);
      std::memset(zeros, 0, full_tile_bytes);
      pixel_data = std::span<const uint8_t>(zeros, full_tile_bytes);
    } else {
      std::cerr << aifocore::fmt::format(
          "Failed to read/decode tile at ({}, {}) in {}: {}\n",
          operation.tile_coord.x, operation.tile_coord.y, reader.GetFilename(),
          decoded_or.status().ToString());
      return aifocore::Status::OkStatus();  // Continue processing other tiles
    }
  } else {
    const DecodedTileData& decoded = *decoded_or;
    pixel_data = decoded.data;
    paint_w = decoded.width;
    paint_h = decoded.height;
    paint_channels = decoded.channels;
  }

  auto write_status = readers::simpletiff_exec::PaintTileMaybeLocked(
      writer, operation, pixel_data, paint_w, paint_h, paint_channels,
      writer_mutex);

  if (!write_status.ok()) {
    std::cerr << "Failed to write tile: " << write_status.ToString();
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  return aifocore::Status::OkStatus();
}

runtime::TileKey AperioTileExecutor::MakeCacheKey(
    const core::TileReadOp& operation, const AperioReader& reader,
    const TiffAccessParams& params) {
  (void)params;
  // Use tile grid coordinates for key (unique per level)
  return runtime::TileKey(reader.GetFilename(),
                          static_cast<uint16_t>(operation.level),
                          operation.tile_coord.x, operation.tile_coord.y);
}

aifocore::Result<DecodedTileData> AperioTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& operation, const AperioReader& reader,
    const TiffAccessParams& params) {
  // Each worker thread uses its own DecodeContext for decompression. This is
  // thread-safe and avoids per-tile allocations.
  static thread_local simpletiff::DecodeContext decode_ctx;
  auto& tile_buffer = GetBuffers().tile_buffer;

  const auto& tiff_index = reader.GetTiffIndex();
  // operation.byte_offset is the linear tile index for tiled TIFFs and the
  // strip index for striped TIFFs (e.g. some SVS associated images / fallback
  // pyramids). The shared helper picks the right code path based on the page
  // header.
  const uint32_t tile_or_strip_index =
      static_cast<uint32_t>(operation.byte_offset);

  AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                            readers::simpletiff_decode::ReadTileOrStrip(
                                tiff_index, static_cast<uint32_t>(params.page),
                                tile_or_strip_index, decode_ctx, tile_buffer));

  // For tiled storage we expect the decoded tile to match the planned tile
  // geometry. Strip storage produces strip-shaped buffers, so don't compare.
  if (params.is_tiled && (decoded.width != params.tile_width ||
                          decoded.height != params.tile_height)) {
    std::cerr << "Tile dimension mismatch: expected " << params.tile_width
              << "x" << params.tile_height << ", got " << decoded.width << "x"
              << decoded.height << "\n";
  }

  return DecodedTileData{decoded.data, decoded.width, decoded.height,
                         decoded.channels};
}

}  // namespace fastslide
