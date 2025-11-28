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

#include <cstring>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/aperio/aperio.h"
#include "fastslide/readers/aperio/aperio_plan_builder.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status AperioTileExecutor::ExecutePlan(
    const core::TilePlan &plan, const AperioReader &reader,
    runtime::TileWriter &writer, const TiffStructureMetadata &tiff_metadata) {
  if (plan.operations.empty()) {
    // No tiles to read - fill with background color
    const auto &bg = plan.output.background;
    return writer.FillWithColor(bg.r, bg.g, bg.b);
  }

  // Execute all tiles sequentially
  // Thread-local buffers provide cache locality benefits without parallelism
  // overhead
  for (const auto &op : plan.operations) {
    auto status = ExecuteTileOperation(
        op, reader, tiff_metadata.page, tiff_metadata.tile_width,
        tiff_metadata.tile_height, tiff_metadata.samples_per_pixel,
        tiff_metadata.is_tiled, writer);
    if (!status.ok()) {
      std::cerr << "Tile at (" << op.tile_coord.x << ", " << op.tile_coord.y
                << ") failed: " << status.ToString();
      // Continue processing remaining tiles
    }
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status AperioTileExecutor::ExecuteTileOperation(
    const core::TileReadOp &op, const AperioReader &reader, uint16_t page,
    uint32_t tile_width, uint32_t tile_height, uint16_t samples_per_pixel,
    bool is_tiled, runtime::TileWriter &writer) {
  // Read and decode the tile (returns span view of thread-local buffer)
  auto tile_data_or = ReadAndDecodeTile(
      op, reader, page, tile_width, tile_height, samples_per_pixel, is_tiled);
  if (!tile_data_or.ok()) {
    std::cerr << "Failed to read/decode tile at (" << op.tile_coord.x << ", "
              << op.tile_coord.y << "): " << tile_data_or.status().ToString();
    return aifocore::Status::OkStatus(); // Continue processing other tiles
  }

  const auto &tile_data = *tile_data_or; // span reference, not vector copy

  // Extract sub-region if needed
  const uint32_t src_x = op.transform.source.x;
  const uint32_t src_y = op.transform.source.y;
  const uint32_t src_width = op.transform.source.width;
  const uint32_t src_height = op.transform.source.height;
  const size_t crop_size = src_width * src_height * samples_per_pixel;

  // Use thread-local buffer from CRTP base class
  // This eliminates the second per-tile allocation
  uint8_t *cropped_data = GetBuffers().GetCropBuffer(crop_size);
  std::memset(cropped_data, 0, crop_size); // Zero initialize

  // Extract the region from the tile buffer
  // IMPORTANT: Use tile_width as stride because TIFF tiles are always
  // allocated with full tile dimensions in memory, even for edge tiles
  for (uint32_t y = 0; y < src_height; ++y) {
    const uint32_t tile_offset =
        ((src_y + y) * tile_width + src_x) * samples_per_pixel;
    const uint32_t dst_offset = y * src_width * samples_per_pixel;
    const uint32_t bytes_to_copy = src_width * samples_per_pixel;

    if (tile_offset + bytes_to_copy <= tile_data.size()) {
      std::memcpy(cropped_data + dst_offset, tile_data.data() + tile_offset,
                  bytes_to_copy);
    } else {
      std::cerr << "Tile bounds check failed at row " << y << " for tile ("
                << op.tile_coord.x << ", " << op.tile_coord.y
                << "): tile_offset=" << tile_offset
                << " + bytes_to_copy=" << bytes_to_copy
                << " > tile_data.size()=" << tile_data.size();
    }
  }

  // Write extracted region
  // Create modified operation with source at (0,0) since we've extracted the
  // sub-region
  core::TileReadOp modified_op = op;
  modified_op.transform.source.x = 0;
  modified_op.transform.source.y = 0;
  modified_op.transform.source.width = src_width;
  modified_op.transform.source.height = src_height;

  auto write_status = writer.WriteTile(
      modified_op, std::span<const uint8_t>(cropped_data, crop_size), src_width,
      src_height, samples_per_pixel);

  if (!write_status.ok()) {
    std::cerr << "Failed to write tile: " << write_status.ToString();
    return aifocore::Status::OkStatus(); // Continue processing other tiles
  }

  return aifocore::Status::OkStatus();
}

aifocore::Result<std::span<const uint8_t>>
AperioTileExecutor::ReadAndDecodeTile(const core::TileReadOp &op,
                                      const AperioReader &reader, uint16_t page,
                                      uint32_t tile_width, uint32_t tile_height,
                                      uint16_t samples_per_pixel,
                                      bool is_tiled) {
  // Each worker thread uses its own DecodeContext for decompression
  // This is thread-safe and avoids per-tile allocations
  static thread_local simpletiff::DecodeContext decode_ctx;
  static thread_local std::vector<uint8_t> tile_buffer;

  // Get TiffIndex from reader
  const auto &tiff_index = reader.GetTiffIndex();

  // op.byte_offset is actually the linear tile index (tile_y * tiles_across +
  // tile_x)
  const uint32_t tile_index = static_cast<uint32_t>(op.byte_offset);

  // Use simpletiff::ReadTile to decompress the tile
  int out_width = 0;
  int out_height = 0;
  auto result = simpletiff::ReadTile(tiff_index, page, tile_index, decode_ctx,
                                     tile_buffer, out_width, out_height);

  if (!result) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read tile ({}, {}): {}",
                              op.tile_coord.x, op.tile_coord.y,
                              result.error().message()));
  }

  // Verify dimensions match expectations
  if (static_cast<uint32_t>(out_width) != tile_width ||
      static_cast<uint32_t>(out_height) != tile_height) {
    std::cerr << "Tile dimension mismatch: expected " << tile_width << "x"
              << tile_height << ", got " << out_width << "x" << out_height;
  }

  // Return span view of thread-local buffer (valid until next call on this
  // thread)
  return std::span<const uint8_t>(tile_buffer.data(), tile_buffer.size());
}

} // namespace fastslide
