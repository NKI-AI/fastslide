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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TILE_EXECUTOR_H_

#include <cstdint>
#include <span>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

// Forward declaration
class AperioReader;

// Forward declaration of TIFF structure metadata
struct TiffStructureMetadata;

/// @brief Tile executor for Aperio SVS slides with thread-local buffer optimization
///
/// Provides sequential tile reading and decoding for Aperio slides with optimized
/// memory management:
/// 1. Acquires TIFF handle from the pool (thread-safe)
/// 2. Sets directory to the correct page (optimized to skip if already set)
/// 3. Reads and decodes JPEG-compressed tiles via libtiff
/// 4. Extracts sub-regions if needed using thread-local buffers
/// 5. Writes to the output buffer
///
/// Thread-local buffers eliminate per-tile allocations and improve cache locality,
/// providing performance benefits in both sequential and parallel contexts.
class AperioTileExecutor : public TiffBasedTileExecutor<AperioTileExecutor> {
 public:
  /// @brief Execute a tile plan sequentially with thread-local buffer optimization
  /// @param plan Pre-computed tile plan from PrepareRequest
  /// @param reader Aperio reader instance for data access
  /// @param writer Tile writer for output buffer management
  /// @param tiff_metadata TIFF structure metadata from plan builder
  /// @return Status indicating success or failure
  /// @note Continues processing even if individual tiles fail (logs warnings)
  static absl::Status ExecutePlan(const core::TilePlan& plan,
                                  const AperioReader& reader,
                                  runtime::TileWriter& writer,
                                  const TiffStructureMetadata& tiff_metadata);

 private:
  /// @brief Execute a single tile operation (called sequentially)
  /// @param op Tile operation descriptor
  /// @param reader Aperio reader instance
  /// @param page TIFF page/directory number for this level
  /// @param tile_width Tile width in pixels
  /// @param tile_height Tile height in pixels
  /// @param samples_per_pixel Number of samples per pixel (typically 3 for RGB)
  /// @param is_tiled Whether TIFF is tiled or stripped
  /// @param writer Tile writer for output
  /// @return Status indicating success or failure
  static absl::Status ExecuteTileOperation(
      const core::TileReadOp& op, const AperioReader& reader, uint16_t page,
      uint32_t tile_width, uint32_t tile_height, uint16_t samples_per_pixel,
      bool is_tiled, runtime::TileWriter& writer);

  /// @brief Read and decode a single TIFF tile/strip
  /// @param op Tile operation descriptor
  /// @param reader Aperio reader instance
  /// @param page TIFF page/directory number
  /// @param tile_width Tile width in pixels
  /// @param tile_height Tile height in pixels (or rows per strip)
  /// @param samples_per_pixel Number of samples per pixel
  /// @param is_tiled Whether TIFF is tiled or stripped
  /// @return Span view of decoded tile data in thread-local buffer or error
  /// @note The returned span is valid until the next call to this function on
  ///       the same thread. Uses thread-local buffers to eliminate per-tile
  ///       allocation overhead in parallel processing.
  static absl::StatusOr<std::span<const uint8_t>> ReadAndDecodeTile(
      const core::TileReadOp& op, const AperioReader& reader, uint16_t page,
      uint32_t tile_width, uint32_t tile_height, uint16_t samples_per_pixel,
      bool is_tiled);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TILE_EXECUTOR_H_
