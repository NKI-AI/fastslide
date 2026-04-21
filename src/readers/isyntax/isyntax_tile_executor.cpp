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

#include "fastslide/readers/isyntax/isyntax_tile_executor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/isyntax/third_party/file.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide {

namespace {

// Helper to convert RGBA to RGB (dropping alpha)
void CopyRgbaToRgb(const uint32_t* src, uint8_t* dst, int width, int height) {
  // libisyntax outputs RGBA/BGRA pixels as uint32_t.
  // Standard format is typically BGRA or RGBA in memory.
  // Assuming little-endian, uint32_t 0xAABBGGRR corresponds to bytes R, G, B, A
  // in memory. We want bytes R, G, B in memory for the output. We can just
  // interpret src as bytes and skip every 4th byte.

  const uint8_t* src_bytes = reinterpret_cast<const uint8_t*>(src);
  int num_pixels = width * height;

  for (int i = 0; i < num_pixels; ++i) {
    dst[0] = src_bytes[0];
    dst[1] = src_bytes[1];
    dst[2] = src_bytes[2];
    dst += 3;
    src_bytes += 4;
  }
}

}  // namespace

aifocore::Status IsyntaxTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                                  const IsyntaxReader& reader,
                                                  runtime::Canvas& writer) {
  if (plan.operations.empty()) {
    // No tiles to read - fill with background color (white)
    return writer.FillBackground(255, 255, 255);
  }

  // Execute all tiles in parallel using the thread pool
  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex accumulator_mutex;
  std::atomic<int> error_count{0};

  // Submit sequence of tile operations
  auto futures = pool.submit_sequence(0, plan.operations.size(), [&](size_t i) {
    const auto& op = plan.operations[i];
    auto status = ExecuteTileOperation(op, reader, writer, accumulator_mutex);

    if (!status.ok()) {
      error_count++;
      if (error_count <= 10) {
        std::cerr << "Tile at (" << op.tile_coord.x << ", " << op.tile_coord.y
                  << ") failed: " << status.ToString() << "\n";
      }
    }
  });

  // Wait for all tiles to complete
  futures.wait();

  if (error_count > 0) {
    std::cerr << "IsyntaxTileExecutor: " << error_count
              << " tiles failed during parallel execution.\n";
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status IsyntaxTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const IsyntaxReader& reader,
    runtime::Canvas& writer, std::mutex& accumulator_mutex) {
  // Get tile size from reader
  ImageDimensions tile_dims = reader.GetTileSize();
  const uint32_t tile_w = static_cast<uint32_t>(tile_dims[0]);
  const uint32_t tile_h = static_cast<uint32_t>(tile_dims[1]);

  // Read and decode the tile (returns span view of thread-local buffer)
  auto tile_data_or = ReadWithCache(op, reader);
  if (!tile_data_or.ok()) {
    std::cerr << "Failed to read/decode tile at (" << op.tile_coord.x << ", "
              << op.tile_coord.y << "): " << tile_data_or.status().ToString();
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  const auto& rgb_span = *tile_data_or;

  // Write to output using Canvas with mutex for thread-safe accumulation
  auto write_status =
      writer.PaintTile(op, rgb_span, tile_w, tile_h, 3, accumulator_mutex);

  if (!write_status.ok()) {
    std::cerr << "Failed to write tile: " << write_status.ToString() << "\n";
    return aifocore::Status::OkStatus();  // Continue processing other tiles
  }

  return aifocore::Status::OkStatus();
}

runtime::TileKey IsyntaxTileExecutor::MakeCacheKey(
    const core::TileReadOp& op, const IsyntaxReader& reader) {
  return runtime::TileKey(std::string(reader.GetFilename()),
                          static_cast<uint16_t>(op.level),
                          static_cast<uint32_t>(op.tile_coord.x),
                          static_cast<uint32_t>(op.tile_coord.y));
}

aifocore::Result<DecodedTileData> IsyntaxTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const IsyntaxReader& reader) {
  // CRITICAL: Ensure thread-local memory is initialized for this worker thread
  // The iSyntax decode pipeline uses thread-local storage that must be
  // initialized before any tile operations. This is safe to call multiple
  // times.
  isyntax::IsyntaxFile::EnsureThreadInit();

  // Get tile size from reader
  ImageDimensions tile_dims = reader.GetTileSize();
  const int32_t tile_w = static_cast<int32_t>(tile_dims[0]);
  const int32_t tile_h = static_cast<int32_t>(tile_dims[1]);

  // Get thread-local buffers
  auto& buffers = GetBuffers();

  // RGBA decode buffer (reused across tiles in this thread)
  const size_t rgba_size = tile_w * tile_h * sizeof(uint32_t);
  uint32_t* rgba_buffer =
      reinterpret_cast<uint32_t*>(buffers.GetTileBuffer(rgba_size));

  // RGB output buffer (reused across tiles in this thread)
  const size_t rgb_size = tile_w * tile_h * 3;
  uint8_t* rgb_buffer = buffers.GetCropBuffer(rgb_size);

  // Lock mutex for libisyntax call (thread-safety for shared isyntax/cache)
  {
    std::lock_guard<std::mutex> lock(reader.GetMutex());

    AIFOCORE_RETURN_IF_ERROR(reader.GetIsyntaxFile().ReadTile(
        op.level, op.tile_coord.x, op.tile_coord.y,
        std::span<uint32_t>(rgba_buffer, tile_w * tile_h),
        isyntax::PixelFormat::kRgba));
  }

  // Convert RGBA to RGB in pooled buffer
  CopyRgbaToRgb(rgba_buffer, rgb_buffer, tile_w, tile_h);

  return DecodedTileData{std::span<const uint8_t>(rgb_buffer, rgb_size),
                         static_cast<uint32_t>(tile_w),
                         static_cast<uint32_t>(tile_h), 3};
}

}  // namespace fastslide
