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

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
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
                                               const MrxsExecContext& context,
                                               runtime::Canvas& writer) {

  if (plan.operations.empty()) {
    // No tiles to read - fill with background color
    const auto& bg = plan.output.background;
    return writer.FillBackground(bg.r, bg.g, bg.b);
  }

  const int level = plan.request.level;
  const auto& slide_info = context.GetMrxsInfo();
  if (level < 0 || level >= static_cast<int>(slide_info.zoom_levels.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& zoom_level = slide_info.zoom_levels[level];

  // Get global thread pool for parallel tile processing
  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex accumulator_mutex;
  std::atomic<int> error_count{0};

  auto futures = pool.submit_sequence(0, plan.operations.size(), [&](size_t i) {
    const auto& op = plan.operations[i];
    auto status = ExecuteTileOperation(op, context, zoom_level, writer,
                                       accumulator_mutex);
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
    const core::TileReadOp& op, const MrxsExecContext& context,
    const mrxs::SlideZoomLevel& zoom_level, runtime::Canvas& writer,
    std::mutex& accumulator_mutex) {

  // Read and decode the tile (returns span view of thread-local buffer)
  auto image_or = ReadWithCache(op, context, zoom_level);
  if (!image_or.ok()) {
    std::cerr << "Failed to read/decode tile at (" << op.tile_coord.x << ", "
              << op.tile_coord.y << "): " << image_or.status().ToString();
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  const auto& tile_data = *image_or;

  const auto& exec_slide_info = context.GetMrxsInfo();
  const uint32_t tile_w = zoom_level.image_width;
  const uint32_t tile_h = zoom_level.image_height;
  const bool is_16bit = exec_slide_info.camera_bitdepth >= 16;
  const uint32_t bytes_per_sample = is_16bit ? 2U : 1U;

  // 3DHISTECH stores fluorescence JPEG tile components in BGR order: a
  // channel declared as `STORING_CHANNEL_NUMBER = S` ends up in JPEG
  // plane `MAX_CHANNELS - 1 - S` after libjpeg/jpgd decode. Mirror the
  // `channel = MAX_CHANNELS - channel - 1` flip from
  // `MiraxReader.java::openBytes` so the storing-channel-sorted output
  // ordering picks the right plane out of the decoded RGB buffer.
  // Empirically only fluorescence JPEG tiles need this inversion; the
  // JPEG-XR identity-copy path already produces samples in
  // storing-channel order.
  const bool invert_jpeg_planes =
      exec_slide_info.slide_type == mrxs::MrxsSlideType::kFluorescence &&
      zoom_level.image_format == mrxs::MrxsImageFormat::kJpeg;

  // For multi-channel (>3) fluorescence the Canvas is configured with
  // PlanarConfig::kSeparate and N channels, so a single RGB-packed PNG
  // contributes 3 planes that need to be written to N >= 4 output channels.
  // We split the decoded buffer into 3 single-channel tiles and paint them
  // one-by-one. For the common N <= 3 case we keep the existing single
  // PaintTile call so the contiguous RGB8 bilinear/blending path is
  // untouched.
  const uint32_t output_channels = writer.GetChannels();
  const bool needs_planar_split = output_channels > 3U;

  if (!needs_planar_split) {
    auto status =
        writer.PaintTile(op, tile_data, tile_w, tile_h, 3, accumulator_mutex);
    (void)status;
    return aifocore::Status::OkStatus();
  }

  // Planar split path: PaintTilePlanar interprets `op.tile_coord.x` as the
  // output channel index. We feed it 3 single-channel tile buffers per
  // decoded PNG, one per R/G/B plane, mapped to channels
  // [channel_group_offset .. channel_group_offset + 2].
  const size_t pixels = static_cast<size_t>(tile_w) * tile_h;
  const size_t plane_bytes = pixels * bytes_per_sample;
  if (tile_data.size() < plane_bytes * 3U) {
    std::cerr << "Tile buffer too small to split into 3 planes (have "
              << tile_data.size() << " bytes, need " << (plane_bytes * 3U)
              << ")\n";
    return aifocore::Status::OkStatus();
  }

  thread_local std::vector<uint8_t> plane_scratch;
  plane_scratch.resize(plane_bytes);

  for (uint32_t plane = 0; plane < 3U; ++plane) {
    // For fluorescence JPEG tiles, JPEG component `plane` actually carries
    // the channel whose `STORING_CHANNEL_NUMBER` is `2 - plane` (the
    // 3DHISTECH encoder writes channels in BGR order). Map the plane to
    // the correct storing-channel slot before computing the output index.
    const uint32_t storing_channel = invert_jpeg_planes ? (2U - plane) : plane;
    const uint32_t out_channel = op.channel_group_offset + storing_channel;

    // Deinterleave one R/G/B plane out of the RGB-packed tile buffer.
    uint64_t sum = 0;
    uint8_t mn = 255, mx = 0;
    if (bytes_per_sample == 1U) {
      const uint8_t* src = tile_data.data() + plane;
      uint8_t* dst = plane_scratch.data();
      for (size_t i = 0; i < pixels; ++i) {
        dst[i] = src[i * 3U];
        sum += dst[i];
        if (dst[i] < mn)
          mn = dst[i];
        if (dst[i] > mx)
          mx = dst[i];
      }
    } else {
      const uint16_t* src =
          reinterpret_cast<const uint16_t*>(tile_data.data()) + plane;
      uint16_t* dst = reinterpret_cast<uint16_t*>(plane_scratch.data());
      for (size_t i = 0; i < pixels; ++i) {
        dst[i] = src[i * 3U];
      }
    }
    const uint64_t mean = pixels > 0 ? sum / pixels : 0;

    core::TileReadOp plane_op = op;
    plane_op.tile_coord.x = out_channel;  // PaintTilePlanar reads this
    auto status = writer.PaintTile(
        plane_op,
        std::span<const uint8_t>(plane_scratch.data(), plane_scratch.size()),
        tile_w, tile_h, /*tile_channels=*/1U, accumulator_mutex);
    (void)status;
  }

  return aifocore::Status::OkStatus();
}

runtime::TileKey MrxsTileExecutor::MakeCacheKey(
    const core::TileReadOp& op, const MrxsExecContext& context,
    const mrxs::SlideZoomLevel& zoom_level) {
  (void)zoom_level;
  // Use dirname from slide info as unique identifier
  // Use data_file_number (source_id) as tile_x and offset as tile_y for
  // uniqueness
  return runtime::TileKey(
      context.GetMrxsInfo().dirname, op.level,
      static_cast<uint32_t>(op.source_id),   // Use data_file_number as tile_x
      static_cast<uint32_t>(op.byte_offset)  // Use offset as tile_y
  );
}

aifocore::Result<DecodedTileData> MrxsTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const MrxsExecContext& context,
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
  auto data_or = context.ReadTileData(tile);
  if (!data_or.ok()) {
    return ToAifoStatus(data_or.status());
  }

  const bool is_16bit = context.GetMrxsInfo().camera_bitdepth >= 16;

  if (is_16bit) {
    auto image_or =
        mrxs::internal::DecodeImage16(*data_or, zoom_level.image_format);
    if (!image_or.ok()) {
      return ToAifoStatus(image_or.status());
    }
    const auto& image = *image_or;
    const size_t data_size = static_cast<size_t>(image.GetWidth()) *
                             image.GetHeight() * 3 * sizeof(uint16_t);
    uint8_t* buffer = GetBuffers().GetTileBuffer(data_size);
    std::memcpy(buffer, image.GetData(), data_size);
    return DecodedTileData{std::span<const uint8_t>(buffer, data_size),
                           image.GetWidth(), image.GetHeight(), 3};
  }

  // 8-bit path
  auto image_or =
      mrxs::internal::DecodeImage(*data_or, zoom_level.image_format);
  if (!image_or.ok()) {
    return ToAifoStatus(image_or.status());
  }

  const auto& image = *image_or;

  const size_t data_size =
      static_cast<size_t>(image.GetWidth()) * image.GetHeight() * 3;
  uint8_t* buffer = GetBuffers().GetTileBuffer(data_size);
  std::memcpy(buffer, image.GetData(), data_size);

  return DecodedTileData{
      std::span<const uint8_t>(buffer, data_size), image.GetWidth(),
      image.GetHeight(),
      3  // RGB
  };
}

}  // namespace fastslide
