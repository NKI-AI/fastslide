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

#include "fastslide/readers/qptiff/qptiff_tile_executor.h"

#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {

absl::Status QptiffTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const std::vector<QpTiffLevelInfo>& pyramid,
    const simpletiff::TiffIndex& tiff_index, runtime::TileWriter& writer) {

  if (plan.operations.empty()) {
    // No tiles to read - fill with background color
    const auto& bg = plan.output.background;
    return writer.FillWithColor(bg.r, bg.g, bg.b);
  }

  const int level = plan.request.level;
  if (level < 0 || static_cast<size_t>(level) >= pyramid.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Invalid level: %d", level));
  }

  const QpTiffLevelInfo& level_info = pyramid[level];
  PageState page_state;

  // Process each tile operation
  for (const auto& op : plan.operations) {
    RETURN_IF_ERROR(
        ExecuteTileOperation(op, level_info, tiff_index, writer, page_state),
        "Failed to execute tile operation");
  }

  return absl::OkStatus();
}

absl::Status QptiffTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const QpTiffLevelInfo& level_info,
    const simpletiff::TiffIndex& tiff_index, runtime::TileWriter& writer,
    PageState& page_state) {

  const uint16_t page = op.source_id;
  const size_t ch_idx = op.tile_coord.x;  // Channel index stored in X coord

  // Update page state if needed
  RETURN_IF_ERROR(UpdatePageState(page, level_info, tiff_index, page_state),
                  "Failed to update page state");

  // Read tile data (returns span view of thread-local buffer)
  auto tile_data_or = ReadTileData(op, tiff_index, page_state);
  if (!tile_data_or.ok()) {
    LOG(WARNING) << "Failed to read tile for channel " << ch_idx << ": "
                 << tile_data_or.status();
    return tile_data_or.status();
  }

  // Extract region (returns span view of thread-local crop buffer)
  const size_t bytes_per_pixel =
      page_state.bytes_per_sample * page_state.samples_per_pixel;
  auto cropped_data = ExtractRegionFromTile(
      *tile_data_or, op, page_state.tile_width, bytes_per_pixel);

  // Write extracted region
  const uint32_t src_width = op.transform.source.width;
  const uint32_t src_height = op.transform.source.height;

  // Create modified operation where source transform starts at (0,0)
  core::TileReadOp modified_op = op;
  modified_op.transform.source.x = 0;
  modified_op.transform.source.y = 0;
  modified_op.transform.source.width = src_width;
  modified_op.transform.source.height = src_height;

  auto write_status =
      writer.WriteTile(modified_op, cropped_data, src_width, src_height,
                       page_state.samples_per_pixel);

  if (!write_status.ok()) {
    LOG(WARNING) << "Failed to write tile for channel " << ch_idx << ": "
                 << write_status;
    return write_status;
  }

  return absl::OkStatus();
}

absl::Status QptiffTileExecutor::UpdatePageState(
    uint16_t page, const QpTiffLevelInfo& level_info,
    const simpletiff::TiffIndex& tiff_index, PageState& page_state) {

  if (page != page_state.current_page) {
    if (page >= tiff_index.NumPages()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Page %d out of range", page));
    }

    const auto& page_header = tiff_index.Page(page);

    page_state.current_page = page;
    page_state.is_tiled = (page_header.storage == simpletiff::Storage::kTiles);

    // Get bits per sample
    page_state.bits_per_sample = page_header.bits_per_sample;
    page_state.bytes_per_sample = (page_state.bits_per_sample + 7) / 8;

    // Get samples per pixel (should be 1 for spectral, 3 for RGB)
    page_state.samples_per_pixel = page_header.samples_per_pixel;

    // Get tile dimensions
    if (page_state.is_tiled) {
      const auto& tiles = tiff_index.Tiles(page_header.payload_id);
      page_state.tile_width = tiles.tile_w;
    } else {
      page_state.tile_width = level_info.size[0];
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<std::span<const uint8_t>> QptiffTileExecutor::ReadTileData(
    const core::TileReadOp& op, const simpletiff::TiffIndex& tiff_index,
    const PageState& page_state) {

  // Each worker thread uses its own DecodeContext for decompression
  // This is thread-safe and avoids per-tile allocations
  static thread_local simpletiff::DecodeContext decode_ctx;
  static thread_local std::vector<uint8_t> tile_buffer;

  const uint16_t page = page_state.current_page;
  const uint32_t tile_index = static_cast<uint32_t>(op.byte_offset);

  // Get page info for debugging
  const auto& page_header = tiff_index.Page(page);

  VLOG(1) << "Worker reading " << (page_state.is_tiled ? "tile" : "strip")
          << " index " << tile_index << " from page " << page
          << " (compression=" << page_header.compression
          << ", storage=" << static_cast<int>(page_header.storage)
          << ", width=" << page_header.width
          << ", height=" << page_header.height << ")";

  // Use appropriate simpletiff function based on storage type
  simpletiff::Result<void> result;
  int out_width = 0;
  int out_height = 0;

  if (page_state.is_tiled) {
    // Read tile for tiled pages
    result = simpletiff::ReadTile(tiff_index, page, tile_index, decode_ctx,
                                  tile_buffer, out_width, out_height);
  } else {
    // Read strip for striped pages
    result = simpletiff::ReadStripe(tiff_index, page, tile_index, decode_ctx,
                                    tile_buffer);
    // For strips, we need to set dimensions manually since ReadStripe doesn't
    // return them
    out_width = static_cast<int>(page_state.tile_width);
    // Height is implicitly handled by the buffer size
  }

  if (!result) {
    LOG(ERROR) << "Failed to read " << (page_state.is_tiled ? "tile" : "strip")
               << " index " << tile_index << " from page " << page
               << ": compression=" << page_header.compression
               << ", storage=" << static_cast<int>(page_header.storage)
               << ", error=" << result.error().message;
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read {} index {}: {}",
                              page_state.is_tiled ? "tile" : "strip",
                              tile_index, result.error().message));
  }

  // Return span view of thread-local buffer (valid until next call on this
  // thread)
  return std::span<const uint8_t>(tile_buffer.data(), tile_buffer.size());
}

std::span<const uint8_t> QptiffTileExecutor::ExtractRegionFromTile(
    std::span<const uint8_t> tile_data, const core::TileReadOp& op,
    uint32_t tile_width, size_t bytes_per_pixel) {

  const uint32_t src_x = op.transform.source.x;
  const uint32_t src_y = op.transform.source.y;
  const uint32_t src_width = op.transform.source.width;
  const uint32_t src_height = op.transform.source.height;
  const size_t crop_size = src_width * src_height * bytes_per_pixel;

  // Use thread-local buffer from CRTP base class
  // This eliminates the second per-tile allocation
  uint8_t* cropped_data = GetBuffers().GetCropBuffer(crop_size);
  std::memset(cropped_data, 0, crop_size);  // Zero initialize

  // Extract the region from the tile buffer
  // IMPORTANT: Use tile_width (not actual dimensions) as stride because
  // TIFF tiles are always allocated with full tile dimensions in memory,
  // even for edge tiles where only part of the data is valid.
  for (uint32_t y = 0; y < src_height; ++y) {
    const size_t tile_offset =
        ((src_y + y) * tile_width + src_x) * bytes_per_pixel;
    const size_t dst_offset = y * src_width * bytes_per_pixel;
    const size_t bytes_to_copy = src_width * bytes_per_pixel;

    if (tile_offset + bytes_to_copy <= tile_data.size()) {
      std::memcpy(cropped_data + dst_offset, tile_data.data() + tile_offset,
                  bytes_to_copy);
    } else {
      LOG(ERROR) << "Tile bounds check failed at row " << y << " for channel "
                 << op.tile_coord.x << ": tile_offset=" << tile_offset
                 << " + bytes_to_copy=" << bytes_to_copy
                 << " > tile_data.size()=" << tile_data.size();
    }
  }

  // Return span view of thread-local buffer (valid until next call on this
  // thread)
  return std::span<const uint8_t>(cropped_data, crop_size);
}

}  // namespace fastslide
