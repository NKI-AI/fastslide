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

#include "fastslide/readers/mrxs/mrxs_tile_executor.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/readers/mrxs/mrxs.h"
#include "fastslide/readers/mrxs/mrxs_decoder.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

namespace {
// Helper to convert status if needed
template <typename T>
aifocore::Status ToAifoStatus(const T& status) {
  if (status.ok())
    return aifocore::Status::OkStatus();
  return aifocore::Status(
      static_cast<aifocore::StatusCode>(static_cast<int>(status.code())),
      std::string(status.message()));
}
}  // namespace

aifocore::Status MrxsTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                               const MrxsReader& reader,
                                               runtime::TileWriter& writer) {

  if (plan.operations.empty()) {
    // No tiles to read - fill with background color
    const auto& bg = plan.output.background;
    return writer.FillBackground(bg.r, bg.g, bg.b);
  }

  const int level = plan.request.level;
  if (level < 0 || level >= reader.GetLevelCount()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& slide_info = reader.GetMrxsInfo();
  const auto& zoom_level = slide_info.zoom_levels[level];

  // Get global thread pool for parallel tile processing
  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex accumulator_mutex;
  std::atomic<int> error_count{0};

  // Submit all tiles to thread pool for parallel processing
  auto futures = pool.submit_sequence(0, plan.operations.size(), [&](size_t i) {
    const auto& op = plan.operations[i];
    auto status =
        ExecuteTileOperation(op, reader, zoom_level, writer, accumulator_mutex);
    if (!status.ok()) {
      error_count++;
      std::cerr << "Tile at (" << op.tile_coord.x << ", " << op.tile_coord.y
                << ") failed: " << status.ToString();
    }
  });

  // Wait for all tiles to complete
  futures.wait();

  if (error_count > 0) {
    std::cerr << error_count << " tile(s) failed during parallel execution"
              << std::endl;
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status MrxsTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const MrxsReader& reader,
    const mrxs::SlideZoomLevel& zoom_level, runtime::TileWriter& writer,
    std::mutex& accumulator_mutex) {

  // Read and decode the tile (returns span view of thread-local buffer)
  auto image_or = ReadWithCache(op, reader, zoom_level);
  if (!image_or.ok()) {
    std::cerr << "Failed to read/decode tile at (" << op.tile_coord.x << ", "
              << op.tile_coord.y << "): " << image_or.status().ToString();
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  // Decode span to get dimensions (we know it's RGB)
  const auto& tile_data = *image_or;
  // For MRXS, we know the tile size from zoom level or op, but ReadWithCache
  // returns just data span. We need width/height. In Qptiff/Aperio, we passed
  // width/height to ReadWithCache/ReadTileFromDisk. Here we pass zoom_level.
  // ReadTileFromDisk returns DecodedTileData which has dims.
  // But ReadWithCache returns span.
  // We need to recover dims.
  // For MRXS, tiles are usually fixed size per level, EXCEPT for edge tiles or
  // different camera positions. Wait, MRXS tiles can have overlaps and
  // different sizes? `zoom_level.image_width`? No, that's full image. The tile
  // size is implicitly defined by the data size for MRXS if we don't store it?
  // We know it is 3 channels (RGB).
  // Area = size / 3.
  // If square, sqrt(Area).
  // But tiles might not be square.
  // We can calculate expected tile size from `op`.
  // `op.byte_size` is compressed size.
  // `op.transform.source` is the region IN THE TILE? No, `transform.source` is
  // the region of the tile to write to output. But `op` does not store full
  // tile size. MRXS tiles (camera images) have a fixed size defined in
  // `SlideZoomLevel`? `zoom_level` has `image_width` (total).
  // `MrxsReader::GetLevelInfo` calculates it.

  // Let's check `MrxsReader::ReadTileData`. It reads `mrxs::MiraxTileRecord`.
  // The record does not have width/height.
  // `mrxs::internal::DecodeImage` decodes it and returns `RGBImage` with proper
  // dims. If we use `CachedTileExecutor`, we lose the explicit dims in the
  // return value of `ReadWithCache`. This is a limitation of `ReadWithCache`
  // returning only `span`.

  // QPTIFF/Aperio handled this by passing expected tile size to
  // `ExecuteTileOperation` and using that. MRXS tiles (camera images) *should*
  // be uniform size? Slidedat.ini defines `DIGITIZER_WIDTH/HEIGHT` which are
  // camera image sizes. Let's look at `mrxs.cpp`: `level.image_width` /
  // `height` in `SlideZoomLevel` ARE the tile sizes (digitizer size)! See
  // `ParseTiledLayers`: level.image_width = ini.GetInt(section,
  // kKeyDigitizerWidth);

  // So we can rely on `zoom_level.image_width` / `image_height`.

  const uint32_t tile_w = zoom_level.image_width;
  const uint32_t tile_h = zoom_level.image_height;

  // Verify size matches data
  if (tile_data.size() != tile_w * tile_h * 3) {
    // If it doesn't match, it might be because of concatenation or other MRXS
    // complexity? Or maybe `DecodeImage` returned different size? Let's trust
    // `tile_w/h` for now, or calculate from size if we assume square? Better to
    // trust `zoom_level` as that's what we used to setup grid.
  }

  // Extract sub-region if needed
  // We can use ExtractSubRegion which now takes span and returns span

  const uint32_t expected_w = op.transform.source.width;
  const uint32_t expected_h = op.transform.source.height;

  std::span<const uint8_t> data_to_write = tile_data;
  uint32_t write_w = tile_w;
  uint32_t write_h = tile_h;
  core::TileReadOp write_op = op;

  if (NeedsSubRegionExtraction(tile_w, tile_h, expected_w, expected_h)) {
    data_to_write = ExtractSubRegion(tile_data, tile_w, tile_h, op);
    // IMPORTANT: ExtractSubRegion() clamps the crop size to the decoded image
    // bounds. We must propagate the *actual* cropped width/height; otherwise
    // BlendedStrategy will read past the end of the crop buffer and produce
    // corrupted output.
    const uint32_t crop_x = op.transform.source.x;
    const uint32_t crop_y = op.transform.source.y;
    if (crop_x >= tile_w || crop_y >= tile_h) {
      return aifocore::Status::OkStatus();  // Nothing to write
    }
    write_w = std::min(expected_w, tile_w - crop_x);
    write_h = std::min(expected_h, tile_h - crop_y);

    // Make the operation consistent with the cropped buffer: source starts at
    // (0,0) and sizes match `write_w/h`.
    write_op.transform.source.x = 0;
    write_op.transform.source.y = 0;
    write_op.transform.source.width = write_w;
    write_op.transform.source.height = write_h;

    // Keep destination sizes consistent with the amount of data we're writing.
    write_op.transform.dest.width =
        std::min(write_op.transform.dest.width, write_w);
    write_op.transform.dest.height =
        std::min(write_op.transform.dest.height, write_h);
  }

  // Write tile to output with mutex for thread-safe accumulation
  auto status =
      writer.WriteTile(write_op, data_to_write, write_w, write_h, 3,  // RGB
                       accumulator_mutex);

  if (!status.ok()) {
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  return aifocore::Status::OkStatus();
}

runtime::TileKey MrxsTileExecutor::MakeCacheKey(
    const core::TileReadOp& op, const MrxsReader& reader,
    const mrxs::SlideZoomLevel& zoom_level) {
  // Use dirname from slide info as unique identifier
  // Use data_file_number (source_id) as tile_x and offset as tile_y for
  // uniqueness
  return runtime::TileKey(
      reader.GetMrxsInfo().dirname, op.level,
      static_cast<uint32_t>(op.source_id),   // Use data_file_number as tile_x
      static_cast<uint32_t>(op.byte_offset)  // Use offset as tile_y
  );
}

aifocore::Result<DecodedTileData> MrxsTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const MrxsReader& reader,
    const mrxs::SlideZoomLevel& zoom_level) {

  // Reconstruct tile info from operation
  mrxs::MiraxTileRecord tile;
  tile.x = op.tile_coord.x;
  tile.y = op.tile_coord.y;
  tile.data_file_number = op.source_id;
  tile.offset = op.byte_offset;
  tile.length = op.byte_size;

  // Get gain from blend metadata if available
  float gain = 1.0f;
  if (op.blend_metadata) {
    gain = op.blend_metadata->gain;
    tile.gain = gain;  // Store in tile for logging
  }

  // Read compressed tile data from disk
  auto data_or = reader.ReadTileData(tile);
  if (!data_or.ok()) {
    return ToAifoStatus(data_or.status());
  }

  // Decode tile
  auto image_or =
      mrxs::internal::DecodeImage(*data_or, zoom_level.image_format);
  if (!image_or.ok()) {
    return ToAifoStatus(image_or.status());
  }

  const auto& image = *image_or;

  // Copy to thread-local buffer
  // This is necessary because RGBImage owns its data and will delete it when it
  // goes out of scope We need to return a span that stays valid (which
  // CachedTileExecutor expects to be from thread-local storage or cache)
  const size_t data_size = image.GetWidth() * image.GetHeight() * 3;
  uint8_t* buffer = GetBuffers().GetTileBuffer(data_size);
  std::memcpy(buffer, image.GetData(), data_size);

  return DecodedTileData{
      std::span<const uint8_t>(buffer, data_size), image.GetWidth(),
      image.GetHeight(),
      3  // RGB
  };
}

std::span<const uint8_t> MrxsTileExecutor::ExtractSubRegion(
    std::span<const uint8_t> image_data, uint32_t img_w, uint32_t img_h,
    const core::TileReadOp& op) {

  const uint32_t crop_x = op.transform.source.x;
  const uint32_t crop_y = op.transform.source.y;
  const uint32_t crop_w = std::min(op.transform.source.width, img_w - crop_x);
  const uint32_t crop_h = std::min(op.transform.source.height, img_h - crop_y);
  const size_t crop_size = crop_w * crop_h * 3;

  // Extract sub-region using row-wise memcpy to thread-local crop buffer
  uint8_t* dst_data = GetBuffers().GetCropBuffer(crop_size);
  const uint8_t* src_data = image_data.data();

  for (uint32_t cy = 0; cy < crop_h; ++cy) {
    const uint32_t src_offset = ((crop_y + cy) * img_w + crop_x) * 3;
    const uint32_t dst_offset = cy * crop_w * 3;
    // Check bounds to be safe
    if (src_offset + crop_w * 3 <= image_data.size()) {
      std::memcpy(dst_data + dst_offset, src_data + src_offset, crop_w * 3);
    }
  }

  return std::span<const uint8_t>(dst_data, crop_size);
}

bool MrxsTileExecutor::NeedsSubRegionExtraction(uint32_t image_width,
                                                uint32_t image_height,
                                                uint32_t expected_width,
                                                uint32_t expected_height) {

  return image_width > expected_width || image_height > expected_height;
}

}  // namespace fastslide
