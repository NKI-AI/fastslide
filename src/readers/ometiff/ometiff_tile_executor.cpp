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

#include "fastslide/readers/ometiff/ometiff_tile_executor.h"

#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status OmetiffTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                                  const OmeTiffReader& reader,
                                                  runtime::TileWriter& writer) {
  const int level = plan.request.level;
  if (level < 0 || level >= reader.GetLevelCount()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& pyramid = reader.GetPyramid();
  const auto& tiff_index = reader.GetTiffIndex();
  const OmeTiffLevelInfo& level_info = pyramid[level];

  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
      plan, writer,
      [&](const core::TileReadOp& operation, runtime::TileWriter& writer_ref,
          std::mutex& writer_mutex) -> aifocore::Status {
        static thread_local PageState page_state;
        if (page_state.index_identity != &tiff_index) {
          page_state = PageState{};
          page_state.index_identity = &tiff_index;
        }
        return ExecuteTileOperation(operation, reader, level_info, tiff_index,
                                    writer_ref, writer_mutex, page_state);
      });
}

aifocore::Status OmetiffTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const OmeTiffReader& reader,
    const OmeTiffLevelInfo& level_info, const simpletiff::TiffIndex& tiff_index,
    runtime::TileWriter& writer, std::mutex& writer_mutex,
    PageState& page_state) {
  const uint32_t page = op.source_id;
  const size_t ch_idx = op.tile_coord.x;

  AIFOCORE_RETURN_IF_ERROR(
      UpdatePageState(page, level_info, tiff_index, page_state));

  auto tile_data_or = ReadWithCache(op, reader, tiff_index, page_state);
  if (!tile_data_or.ok()) {
    std::cerr << "Failed to read tile for channel " << ch_idx << ": "
              << tile_data_or.status().ToString();
    return tile_data_or.status();
  }

  const size_t bytes_per_pixel =
      page_state.bytes_per_sample * page_state.samples_per_pixel;
  auto cropped_data = ExtractRegionFromTile(
      *tile_data_or, op, page_state.tile_width, bytes_per_pixel);

  const uint32_t src_width = op.transform.source.width;
  const uint32_t src_height = op.transform.source.height;

  core::TileReadOp modified_op = op;
  modified_op.transform.source.x = 0;
  modified_op.transform.source.y = 0;
  modified_op.transform.source.width = src_width;
  modified_op.transform.source.height = src_height;

  auto write_status = readers::simpletiff_exec::WriteTileMaybeLocked(
      writer, modified_op, cropped_data, src_width, src_height,
      page_state.samples_per_pixel, writer_mutex);
  if (!write_status.ok()) {
    std::cerr << "Failed to write tile for channel " << ch_idx << ": "
              << write_status.ToString();
    return write_status;
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status OmetiffTileExecutor::UpdatePageState(
    uint32_t page, const OmeTiffLevelInfo& level_info,
    const simpletiff::TiffIndex& tiff_index, PageState& page_state) {
  if (page == page_state.current_page) {
    return aifocore::Status::OkStatus();
  }

  if (page >= tiff_index.NumPages()) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Page {} out of range", page));
  }

  const auto& page_header = tiff_index.Page(page);

  page_state.current_page = page;
  page_state.is_tiled = (page_header.storage == simpletiff::Storage::kTiles);
  page_state.bits_per_sample = page_header.bits_per_sample;
  page_state.bytes_per_sample = (page_state.bits_per_sample + 7) / 8;
  page_state.samples_per_pixel = page_header.samples_per_pixel;

  if (page_state.is_tiled) {
    const auto& tiles = tiff_index.Tiles(page_header.payload_id);
    page_state.tile_width = tiles.tile_w;
    page_state.tile_height = tiles.tile_h;
  } else {
    page_state.tile_width = level_info.size[0];
    page_state.tile_height = level_info.size[1];
  }

  return aifocore::Status::OkStatus();
}

runtime::TileKey OmetiffTileExecutor::MakeCacheKey(
    const core::TileReadOp& op, const OmeTiffReader& reader,
    const simpletiff::TiffIndex& tiff_index, const PageState& page_state) {
  (void)tiff_index;
  (void)page_state;
  return runtime::TileKey(reader.GetFilename(), op.source_id,
                          static_cast<uint32_t>(op.byte_offset), 0);
}

aifocore::Result<DecodedTileData> OmetiffTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const OmeTiffReader& reader,
    const simpletiff::TiffIndex& tiff_index, const PageState& page_state) {
  (void)reader;
  auto& tile_buffer = GetBuffers().tile_buffer;
  static thread_local simpletiff::DecodeContext decode_ctx;

  const uint32_t page = page_state.current_page;
  const uint32_t tile_index = static_cast<uint32_t>(op.byte_offset);

  simpletiff::Result<void> result;
  int out_width = 0;
  int out_height = 0;

  if (page_state.is_tiled) {
    result = simpletiff::ReadTile(tiff_index, page, tile_index, decode_ctx,
                                  tile_buffer, out_width, out_height);
  } else {
    result = simpletiff::ReadStripe(tiff_index, page, tile_index, decode_ctx,
                                    tile_buffer);
    out_width = static_cast<int>(page_state.tile_width);
    out_height = static_cast<int>(page_state.tile_height);
  }

  if (!result) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read {} index {}: {}",
                              page_state.is_tiled ? "tile" : "strip",
                              tile_index, result.error().message()));
  }

  return DecodedTileData{
      std::span<const uint8_t>(tile_buffer.data(), tile_buffer.size()),
      static_cast<uint32_t>(out_width), static_cast<uint32_t>(out_height),
      static_cast<uint32_t>(page_state.samples_per_pixel)};
}

std::span<const uint8_t> OmetiffTileExecutor::ExtractRegionFromTile(
    std::span<const uint8_t> tile_data, const core::TileReadOp& op,
    uint32_t tile_width, size_t bytes_per_pixel) {
  const uint32_t src_x = op.transform.source.x;
  const uint32_t src_y = op.transform.source.y;
  const uint32_t src_width = op.transform.source.width;
  const uint32_t src_height = op.transform.source.height;
  const size_t crop_size = src_width * src_height * bytes_per_pixel;

  uint8_t* cropped_data = GetBuffers().GetCropBuffer(crop_size);
  std::memset(cropped_data, 0, crop_size);

  for (uint32_t y = 0; y < src_height; ++y) {
    const size_t tile_offset =
        ((src_y + y) * tile_width + src_x) * bytes_per_pixel;
    const size_t dst_offset = y * src_width * bytes_per_pixel;
    const size_t bytes_to_copy = src_width * bytes_per_pixel;
    if (tile_offset + bytes_to_copy <= tile_data.size()) {
      std::memcpy(cropped_data + dst_offset, tile_data.data() + tile_offset,
                  bytes_to_copy);
    }
  }

  return std::span<const uint8_t>(cropped_data, crop_size);
}

}  // namespace fastslide
