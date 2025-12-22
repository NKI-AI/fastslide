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
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status AperioTileExecutor::ExecutePlan(
    const core::TilePlan &plan, const AperioReader &reader,
    runtime::TileWriter &writer, const TiffStructureMetadata &tiff_metadata) {
  std::atomic<int> error_count{0};
  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolBestEffort(
      plan, writer,
      [&](const core::TileReadOp &operation, runtime::TileWriter &writer_ref,
          std::mutex &writer_mutex) -> aifocore::Status {
        return ExecuteTileOperation(
            operation, reader, tiff_metadata.page, tiff_metadata.tile_width,
            tiff_metadata.tile_height, tiff_metadata.samples_per_pixel,
            tiff_metadata.is_tiled, writer_ref, writer_mutex);
      },
      [&](const core::TileReadOp &operation, const aifocore::Status &status) {
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
    const core::TileReadOp &operation, const AperioReader &reader,
    uint16_t page, uint32_t tile_width, uint32_t tile_height,
    uint16_t samples_per_pixel, bool is_tiled, runtime::TileWriter &writer,
    std::mutex &writer_mutex) {
  const TiffAccessParams tiff_params = {
      .page = page,
      .tile_width = tile_width,
      .tile_height = tile_height,
      .samples_per_pixel = samples_per_pixel,
      .is_tiled = is_tiled,
  };

  // Read and decode the tile (returns span view of thread-local buffer)
  auto tile_data_or = ReadWithCache(operation, reader, tiff_params);
  if (!tile_data_or.ok()) {
    std::cerr << "Failed to read/decode tile at (" << operation.tile_coord.x
              << ", " << operation.tile_coord.y
              << "): " << tile_data_or.status().ToString();
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  const auto &tile_data = *tile_data_or; // span reference, not vector copy

  // Extract sub-region if needed
  const uint32_t src_x = operation.transform.source.x;
  const uint32_t src_y = operation.transform.source.y;
  const uint32_t src_width = operation.transform.source.width;
  const uint32_t src_height = operation.transform.source.height;
  const size_t crop_size = src_width * src_height * samples_per_pixel;

  // Use thread-local buffer from CRTP base class
  // This eliminates the second per-tile allocation
  uint8_t *cropped_data =
      TiffBasedTileExecutor<AperioTileExecutor>::GetBuffers().GetCropBuffer(
          crop_size);
  std::memset(cropped_data, 0, crop_size);  // Zero initialize

  // Extract the region from the tile buffer
  // IMPORTANT: Use tile_width as stride because TIFF tiles are always
  // allocated with full tile dimensions in memory, even for edge tiles
  for (uint32_t row = 0; row < src_height; ++row) {
    const uint32_t tile_offset =
        ((src_y + row) * tile_width + src_x) * samples_per_pixel;
    const uint32_t dst_offset = row * src_width * samples_per_pixel;
    const uint32_t bytes_to_copy = src_width * samples_per_pixel;

    if (tile_offset + bytes_to_copy <= tile_data.size()) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      std::memcpy(cropped_data + dst_offset, tile_data.data() + tile_offset,
                  bytes_to_copy);
    } else {
      std::cerr << "Tile bounds check failed at row " << row << " for tile ("
                << operation.tile_coord.x << ", " << operation.tile_coord.y
                << "): tile_offset=" << tile_offset
                << " + bytes_to_copy=" << bytes_to_copy
                << " > tile_data.size()=" << tile_data.size();
    }
  }

  // Write extracted region
  // Create modified operation with source at (0,0) since we've extracted the
  // sub-region
  core::TileReadOp modified_op = operation;
  modified_op.transform.source.x = 0;
  modified_op.transform.source.y = 0;
  modified_op.transform.source.width = src_width;
  modified_op.transform.source.height = src_height;

  auto write_status = readers::simpletiff_exec::WriteTileMaybeLocked(
      writer, modified_op, std::span<const uint8_t>(cropped_data, crop_size),
      src_width, src_height, samples_per_pixel, writer_mutex);

  if (!write_status.ok()) {
    std::cerr << "Failed to write tile: " << write_status.ToString();
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  return aifocore::Status::OkStatus();
}

runtime::TileKey
AperioTileExecutor::MakeCacheKey(const core::TileReadOp &operation,
                                 const AperioReader &reader,
                                 const TiffAccessParams &params) {
  (void)params;
  // Use tile grid coordinates for key (unique per level)
  return runtime::TileKey(reader.GetFilename(),
                          static_cast<uint16_t>(operation.level),
                          operation.tile_coord.x, operation.tile_coord.y);
}

aifocore::Result<DecodedTileData>
AperioTileExecutor::ReadTileFromDisk(const core::TileReadOp &operation,
                                     const AperioReader &reader,
                                     const TiffAccessParams &params) {
  // Each worker thread uses its own DecodeContext for decompression
  // This is thread-safe and avoids per-tile allocations
  static thread_local simpletiff::DecodeContext decode_ctx;
  auto &tile_buffer = GetBuffers().tile_buffer;

  // Get TiffIndex from reader
  const auto &tiff_index = reader.GetTiffIndex();

  // op.byte_offset is actually the linear tile index (tile_y * tiles_across +
  // tile_x)
  const uint32_t tile_index = static_cast<uint32_t>(operation.byte_offset);

  // Use simpletiff::ReadTile to decompress the tile
  int out_width = 0;
  int out_height = 0;
  auto result =
      simpletiff::ReadTile(tiff_index, params.page, tile_index, decode_ctx,
                           tile_buffer, out_width, out_height);

  if (!result) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read tile ({}, {}): {}",
                              operation.tile_coord.x, operation.tile_coord.y,
                              result.error().message()));
  }

  // Verify dimensions match expectations
  if (static_cast<uint32_t>(out_width) != params.tile_width ||
      static_cast<uint32_t>(out_height) != params.tile_height) {
    std::cerr << "Tile dimension mismatch: expected " << params.tile_width
              << "x" << params.tile_height << ", got " << out_width << "x"
              << out_height;
  }

  return DecodedTileData{
      std::span<const uint8_t>(tile_buffer.data(), tile_buffer.size()),
      static_cast<uint32_t>(out_width), static_cast<uint32_t>(out_height),
      static_cast<uint32_t>(params.samples_per_pixel)};
}

}  // namespace fastslide
