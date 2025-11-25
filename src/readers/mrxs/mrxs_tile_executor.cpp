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
#include <mutex>
#include <span>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/readers/mrxs/mrxs.h"
#include "fastslide/readers/mrxs/mrxs_decoder.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

absl::Status MrxsTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                           const MrxsReader& reader,
                                           runtime::TileWriter& writer) {

  if (plan.operations.empty()) {
    // No tiles to read - fill with background color
    const auto& bg = plan.output.background;
    return writer.FillWithColor(bg.r, bg.g, bg.b);
  }

  const int level = plan.request.level;
  if (level < 0 || level >= reader.GetLevelCount()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid level: %d", level));
  }

  const auto& slide_info = reader.GetMrxsInfo();
  const auto& zoom_level = slide_info.zoom_levels[level];

  // Get global thread pool for parallel tile processing
  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  absl::Mutex accumulator_mutex;
  std::atomic<int> error_count{0};

  // Submit all tiles to thread pool for parallel processing
  auto futures = pool.submit_sequence(0, plan.operations.size(), [&](size_t i) {
    const auto& op = plan.operations[i];
    auto status =
        ExecuteTileOperation(op, reader, zoom_level, writer, accumulator_mutex);
    if (!status.ok()) {
      error_count++;
      LOG(WARNING) << "Tile at (" << op.tile_coord.x << ", " << op.tile_coord.y
                   << ") failed: " << status;
    }
  });

  // Wait for all tiles to complete
  futures.wait();

  if (error_count > 0) {
    LOG(WARNING) << error_count << " tile(s) failed during parallel execution";
  }

  return absl::OkStatus();
}

absl::Status MrxsTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const MrxsReader& reader,
    const mrxs::SlideZoomLevel& zoom_level, runtime::TileWriter& writer,
    absl::Mutex& accumulator_mutex) {

  // Read and decode the tile
  auto image_or = ReadAndDecodeTile(op, reader, zoom_level);
  if (!image_or.ok()) {
    LOG(WARNING) << "Failed to read/decode tile at (" << op.tile_coord.x << ", "
                 << op.tile_coord.y << "): " << image_or.status();
    return absl::OkStatus();  // Continue processing other tiles
  }

  // Extract sub-region if needed
  RGBImage tile_to_write = *image_or;
  const uint32_t img_w = image_or->GetWidth();
  const uint32_t img_h = image_or->GetHeight();
  const uint32_t expected_w = op.transform.source.width;
  const uint32_t expected_h = op.transform.source.height;

  if (NeedsSubRegionExtraction(img_w, img_h, expected_w, expected_h)) {
    tile_to_write = ExtractSubRegion(*image_or, op);
  }

  // Write tile to output with mutex for thread-safe accumulation
  auto status = writer.WriteTile(
      op,
      std::span<const uint8_t>(tile_to_write.GetData(),
                               tile_to_write.GetDataVector().size()),
      tile_to_write.GetWidth(), tile_to_write.GetHeight(), 3,  // RGB
      accumulator_mutex);

  if (!status.ok()) {
    LOG(WARNING) << "Failed to write tile: " << status;
    return absl::OkStatus();  // Continue processing other tiles
  }

  return absl::OkStatus();
}

absl::StatusOr<RGBImage> MrxsTileExecutor::ReadAndDecodeTile(
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

  // Try cache first if available
  auto cache = reader.GetITileCache();
  if (cache) {
    // Create cache key for this camera image
    // Use dirname from slide info as unique identifier
    runtime::TileKey cache_key(
        reader.GetMrxsInfo().dirname, op.level,
        static_cast<uint32_t>(op.source_id),   // Use data_file_number as tile_x
        static_cast<uint32_t>(op.byte_offset)  // Use offset as tile_y
    );

    auto cached_data = cache->Get(cache_key);
    if (cached_data) {
      // Reconstruct image from cached data
      RGBImage image(cached_data->size, ImageFormat::kRGB, DataType::kUInt8);
      std::memcpy(image.GetData(), cached_data->data.data(),
                  cached_data->data.size());
      return image;
    }
  }

  // Read compressed tile data from disk
  auto data_or = reader.ReadTileData(tile);
  if (!data_or.ok()) {
    return data_or.status();
  }

  // Decode tile
  auto image_or =
      mrxs::internal::DecodeImage(*data_or, zoom_level.image_format);
  if (!image_or.ok()) {
    return image_or.status();
  }

  // Cache the decoded image if cache is available
  if (cache) {
    runtime::TileKey cache_key(reader.GetMrxsInfo().dirname, op.level,
                               static_cast<uint32_t>(op.source_id),
                               static_cast<uint32_t>(op.byte_offset));

    // Create cached tile data
    const auto& image = *image_or;
    const size_t data_size = image.GetWidth() * image.GetHeight() * 3;
    std::vector<uint8_t> tile_data(data_size);
    std::memcpy(tile_data.data(), image.GetData(), data_size);

    auto cached_tile = std::make_shared<runtime::CachedTileData>(
        std::move(tile_data),
        aifocore::Size<uint32_t, 2>{image.GetWidth(), image.GetHeight()},
        3  // RGB channels
    );

    cache->Put(cache_key, cached_tile);
  }

  return *image_or;
}

RGBImage MrxsTileExecutor::ExtractSubRegion(const RGBImage& image,
                                            const core::TileReadOp& op) {

  const uint32_t img_w = image.GetWidth();
  const uint32_t img_h = image.GetHeight();
  const uint32_t crop_x = op.transform.source.x;
  const uint32_t crop_y = op.transform.source.y;
  const uint32_t crop_w = std::min(op.transform.source.width, img_w - crop_x);
  const uint32_t crop_h = std::min(op.transform.source.height, img_h - crop_y);

  // Extract sub-region using row-wise memcpy
  RGBImage extracted({crop_w, crop_h}, ImageFormat::kRGB, DataType::kUInt8);
  uint8_t* dst_data = extracted.GetData();
  const uint8_t* src_data = image.GetData();

  for (uint32_t cy = 0; cy < crop_h; ++cy) {
    const uint32_t src_offset = ((crop_y + cy) * img_w + crop_x) * 3;
    const uint32_t dst_offset = cy * crop_w * 3;
    std::memcpy(dst_data + dst_offset, src_data + src_offset, crop_w * 3);
  }

  return extracted;
}

bool MrxsTileExecutor::NeedsSubRegionExtraction(uint32_t image_width,
                                                uint32_t image_height,
                                                uint32_t expected_width,
                                                uint32_t expected_height) {

  return image_width > expected_width || image_height > expected_height;
}

}  // namespace fastslide
