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

#include <tiffio.h>
#include <cstring>
#include <span>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/aperio/aperio.h"
#include "fastslide/readers/aperio/aperio_plan_builder.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/tiff/tiff_file.h"

namespace fastslide {

absl::Status AperioTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const AperioReader& reader,
    runtime::TileWriter& writer, const TiffStructureMetadata& tiff_metadata) {
  if (plan.operations.empty()) {
    // No tiles to read - fill with background color
    const auto& bg = plan.output.background;
    return writer.FillWithColor(bg.r, bg.g, bg.b);
  }

  // Execute all tiles sequentially
  // Thread-local buffers provide cache locality benefits without parallelism overhead
  for (const auto& op : plan.operations) {
    auto status = ExecuteTileOperation(
        op, reader, tiff_metadata.page, tiff_metadata.tile_width,
        tiff_metadata.tile_height, tiff_metadata.samples_per_pixel,
        tiff_metadata.is_tiled, writer);
    if (!status.ok()) {
      LOG(WARNING) << "Tile at (" << op.tile_coord.x << ", " << op.tile_coord.y
                   << ") failed: " << status;
      // Continue processing remaining tiles
    }
  }

  return absl::OkStatus();
}

absl::Status AperioTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const AperioReader& reader, uint16_t page,
    uint32_t tile_width, uint32_t tile_height, uint16_t samples_per_pixel,
    bool is_tiled, runtime::TileWriter& writer) {
  // Read and decode the tile (returns span view of thread-local buffer)
  auto tile_data_or = ReadAndDecodeTile(
      op, reader, page, tile_width, tile_height, samples_per_pixel, is_tiled);
  if (!tile_data_or.ok()) {
    LOG(WARNING) << "Failed to read/decode tile at (" << op.tile_coord.x << ", "
                 << op.tile_coord.y << "): " << tile_data_or.status();
    return absl::OkStatus();  // Continue processing other tiles
  }

  const auto& tile_data = *tile_data_or;  // span reference, not vector copy

  // Extract sub-region if needed
  const uint32_t src_x = op.transform.source.x;
  const uint32_t src_y = op.transform.source.y;
  const uint32_t src_width = op.transform.source.width;
  const uint32_t src_height = op.transform.source.height;
  const size_t crop_size = src_width * src_height * samples_per_pixel;

  // Use thread-local buffer from CRTP base class
  // This eliminates the second per-tile allocation
  uint8_t* cropped_data = GetBuffers().GetCropBuffer(crop_size);
  std::memset(cropped_data, 0, crop_size);  // Zero initialize

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
      LOG(ERROR) << "Tile bounds check failed at row " << y << " for tile ("
                 << op.tile_coord.x << ", " << op.tile_coord.y
                 << "): tile_offset=" << tile_offset
                 << " + bytes_to_copy=" << bytes_to_copy
                 << " > tile_data.size()=" << tile_data.size();
    }
  }

  // Write extracted region
  // Create modified operation with source at (0,0) since we've extracted the sub-region
  core::TileReadOp modified_op = op;
  modified_op.transform.source.x = 0;
  modified_op.transform.source.y = 0;
  modified_op.transform.source.width = src_width;
  modified_op.transform.source.height = src_height;

  auto write_status = writer.WriteTile(
      modified_op, std::span<const uint8_t>(cropped_data, crop_size), src_width,
      src_height, samples_per_pixel);

  if (!write_status.ok()) {
    LOG(WARNING) << "Failed to write tile: " << write_status;
    return absl::OkStatus();  // Continue processing other tiles
  }

  return absl::OkStatus();
}

absl::StatusOr<std::span<const uint8_t>> AperioTileExecutor::ReadAndDecodeTile(
    const core::TileReadOp& op, const AperioReader& reader, uint16_t page,
    uint32_t tile_width, uint32_t tile_height, uint16_t samples_per_pixel,
    bool is_tiled) {
  // Each worker thread gets its own TiffFile from the handle pool
  // This is thread-safe because the pool manages per-thread handles
  VLOG(1) << "Worker acquiring TIFF handle for tile (" << op.tile_coord.x
          << ", " << op.tile_coord.y << ")";
  auto tiff_file_result = TiffFile::Create(reader.GetHandlePool());
  if (!tiff_file_result.ok()) {
    return tiff_file_result.status();
  }
  auto tiff_file = std::move(tiff_file_result.value());
  VLOG(1) << "Worker acquired handle, reading tile (" << op.tile_coord.x << ", "
          << op.tile_coord.y << ")";

  // Set directory - optimized to skip if already on this directory
  auto status = tiff_file.SetDirectory(page);
  if (!status.ok()) {
    return status;
  }

  TIFF* tif = tiff_file.GetHandle();

  // Read the TIFF tile/strip
  tmsize_t tile_size = is_tiled ? TIFFTileSize(tif) : TIFFStripSize(tif);
  if (tile_size <= 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        aifocore::fmt::format("Invalid tile size for tile ({}, {})",
                              op.tile_coord.x, op.tile_coord.y));
  }

  // Use thread-local buffer from CRTP base class
  // This eliminates per-tile allocation overhead
  uint8_t* tile_data = GetBuffers().GetTileBuffer(tile_size);

  tmsize_t bytes_read;
  if (is_tiled) {
    bytes_read = TIFFReadEncodedTile(tif, op.byte_offset, tile_data, tile_size);
  } else {
    bytes_read =
        TIFFReadEncodedStrip(tif, op.byte_offset, tile_data, tile_size);
  }

  if (bytes_read <= 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       aifocore::fmt::format("Failed to read tile ({}, {})",
                                             op.tile_coord.x, op.tile_coord.y));
  }

  // Return span view of thread-local buffer (valid until next call on this thread)
  return std::span<const uint8_t>(tile_data, bytes_read);
}

}  // namespace fastslide
