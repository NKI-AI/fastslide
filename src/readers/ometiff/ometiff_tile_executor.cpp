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
#include "fastslide/readers/simpletiff_decode_utils.h"
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
    const core::TileReadOp& operation, const OmeTiffReader& reader,
    const OmeTiffLevelInfo& level_info, const simpletiff::TiffIndex& tiff_index,
    runtime::TileWriter& writer, std::mutex& writer_mutex,
    PageState& page_state) {
  const uint32_t page = operation.source_id;
  const size_t channel_index = operation.tile_coord.x;

  AIFOCORE_RETURN_IF_ERROR(
      UpdatePageState(page, level_info, tiff_index, page_state));

  auto decoded_tile_or =
      ReadWithCacheDecoded(operation, reader, tiff_index, page_state);
  if (!decoded_tile_or.ok()) {
    std::cerr << "Failed to read tile for channel " << channel_index << ": "
              << decoded_tile_or.status().ToString();
    return decoded_tile_or.status();
  }

  const size_t bytes_per_pixel =
      static_cast<size_t>(page_state.bytes_per_sample) *
      static_cast<size_t>((*decoded_tile_or).channels);
  auto cropped_data_or =
      ExtractRegionFromTile(*decoded_tile_or, operation, bytes_per_pixel);
  if (!cropped_data_or.ok()) {
    std::cerr << "Failed to crop tile for channel " << channel_index << ": "
              << cropped_data_or.status().ToString();
    return cropped_data_or.status();
  }

  const uint32_t src_width = operation.transform.source.width;
  const uint32_t src_height = operation.transform.source.height;

  core::TileReadOp modified_op = operation;
  modified_op.transform.source.x = 0;
  modified_op.transform.source.y = 0;
  modified_op.transform.source.width = src_width;
  modified_op.transform.source.height = src_height;

  auto write_status = readers::simpletiff_exec::WriteTileMaybeLocked(
      writer, modified_op, *cropped_data_or, src_width, src_height,
      (*decoded_tile_or).channels, writer_mutex);
  if (!write_status.ok()) {
    std::cerr << "Failed to write tile for channel " << channel_index << ": "
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
    const core::TileReadOp& operation, const OmeTiffReader& reader,
    const simpletiff::TiffIndex& tiff_index, const PageState& page_state) {
  (void)tiff_index;
  (void)page_state;
  return runtime::TileKey(reader.GetFilename(), operation.source_id,
                          static_cast<uint32_t>(operation.byte_offset), 0);
}

aifocore::Result<DecodedTileData> OmetiffTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& operation, const OmeTiffReader& reader,
    const simpletiff::TiffIndex& tiff_index, const PageState& page_state) {
  (void)reader;
  auto& tile_buffer = GetBuffers().tile_buffer;
  static thread_local simpletiff::DecodeContext decode_ctx;

  const uint32_t page = page_state.current_page;
  const uint32_t tile_index = static_cast<uint32_t>(operation.byte_offset);

  const uint32_t page_index = page;
  const uint32_t tile_or_strip_index = tile_index;
  AIFOCORE_ASSIGN_OR_RETURN(
      auto decoded, readers::simpletiff_decode::ReadTileOrStrip(
                        tiff_index, page_index, tile_or_strip_index, decode_ctx,
                        tile_buffer));
  return DecodedTileData{decoded.data, decoded.width, decoded.height,
                         decoded.channels};
}

aifocore::Result<std::span<const uint8_t>>
OmetiffTileExecutor::ExtractRegionFromTile(const DecodedTileData& decoded_tile,
                                           const core::TileReadOp& operation,
                                           size_t bytes_per_pixel) {
  const uint32_t src_x = operation.transform.source.x;
  const uint32_t src_y = operation.transform.source.y;
  const uint32_t src_width = operation.transform.source.width;
  const uint32_t src_height = operation.transform.source.height;
  const size_t crop_size = src_width * src_height * bytes_per_pixel;

  uint8_t* cropped_data = GetBuffers().GetCropBuffer(crop_size);

  const auto cropped_span = std::span<uint8_t>(cropped_data, crop_size);
  return readers::simpletiff_decode::CropInterleavedRoi(
      decoded_tile.data, decoded_tile.width, decoded_tile.height,
      readers::simpletiff_decode::RectU32{
          .x = src_x, .y = src_y, .width = src_width, .height = src_height},
      bytes_per_pixel, cropped_span);
}

}  // namespace fastslide
