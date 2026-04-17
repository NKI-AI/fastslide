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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_TILE_EXECUTOR_H_

#include <limits>
#include <mutex>
#include <span>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/qptiff/qptiff.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"

/**
 * @file qptiff_tile_executor.h
 * @brief QPTIFF tile plan executor with thread-local buffer optimization
 *
 * This header defines QptiffTileExecutor, a helper class for executing
 * tile reading plans for QPTIFF images. This is stage 2 of the two-stage
 * pipeline, which performs I/O and decoding based on a prepared plan.
 *
 * **Responsibilities:**
 * - Read compressed tile data from TIFF pages
 * - Decompress tiles via simpletiff
 * - Handle multi-channel data (RGB or spectral)
 * - Apply transformations (crop, scale, channel selection)
 * - Write decoded data to the destination writer
 *
 * **QPTIFF Specifics:**
 * - Reads from multiple TIFF pages for multi-channel images
 * - Supports both planar (separated) and interleaved layouts
 * - Handles channel-specific tile sizes
 * - Efficiently processes only visible channels
 *
 * **Performance:**
 * - Thread-local buffers eliminate per-tile allocations
 * - Improved cache locality from buffer reuse
 * - Can leverage tile cache to skip I/O for cached tiles
 * - Sequential execution with optimized memory management
 *
 * @see QptiffPlanBuilder for stage 1 (plan creation)
 * @see QpTiffReader for the main reader class
 */

namespace fastslide {

/// @brief Helper class for executing QPTIFF tile read operations with
/// thread-local buffers
class QptiffTileExecutor : public CachedTileExecutor<QptiffTileExecutor> {
 public:
  /// @brief Execute a tile plan
  /// @param plan The tile plan to execute
  /// @param reader QPTIFF reader instance
  /// @param writer Tile writer for output
  /// @return Status indicating success or failure
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const QpTiffReader& reader,
                                      runtime::TileWriter& writer);

  /// @brief Friend declaration for CachedTileExecutor to access
  /// protected/private members
  friend class CachedTileExecutor<QptiffTileExecutor>;

 private:
  /// @brief State for tracking TIFF page information during execution
  struct PageState {
    const simpletiff::TiffIndex* index_identity = nullptr;
    uint16_t current_page = std::numeric_limits<uint16_t>::max();
    bool is_tiled = false;
    uint32_t tile_width = 0;
    uint32_t tile_height = 0;  // Use full tile height including strips
    uint16_t samples_per_pixel = 1;
    uint16_t bits_per_sample = 8;
    uint32_t bytes_per_sample = 1;
  };

  /// @brief Execute a single tile operation
  /// @param op The tile operation to execute
  /// @param reader QPTIFF reader instance
  /// @param level_info Level information
  /// @param tiff_index SimpleTiff index
  /// @param writer Tile writer
  /// @param page_state Current page state (updated if needed)
  /// @return Status indicating success or failure
  static aifocore::Status ExecuteTileOperation(
      const core::TileReadOp& operation, const QpTiffReader& reader,
      const QpTiffLevelInfo& level_info,
      const simpletiff::TiffIndex& tiff_index, runtime::TileWriter& writer,
      std::mutex& writer_mutex, PageState& page_state);

  /// @brief Update page state if page changed
  /// @param page New page number
  /// @param level_info Level information
  /// @param tiff_index SimpleTiff index
  /// @param page_state Page state to update
  /// @return Status indicating success or failure
  static aifocore::Status UpdatePageState(
      uint16_t page, const QpTiffLevelInfo& level_info,
      const simpletiff::TiffIndex& tiff_index, PageState& page_state);

  // CachedTileExecutor interface implementation

  /// @brief Create cache key for a tile
  static TileKey MakeCacheKey(const core::TileReadOp& operation,
                              const QpTiffReader& reader,
                              const simpletiff::TiffIndex& tiff_index,
                              const PageState& page_state);

  /// @brief Read tile data from disk (called on cache miss)
  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& operation, const QpTiffReader& reader,
      const simpletiff::TiffIndex& tiff_index, const PageState& page_state);

  /// @brief Extract region from tile buffer to thread-local crop buffer
  /// @param tile_data Full tile data span
  /// @param op Tile operation with transform information
  /// @param bytes_per_pixel Bytes per pixel
  /// @return Span view of extracted region in thread-local buffer
  /// @note The returned span is valid until the next call to this function on
  ///       the same thread.
  static aifocore::Result<std::span<const uint8_t>> ExtractRegionFromTile(
      const DecodedTileData& decoded_tile, const core::TileReadOp& operation,
      size_t bytes_per_pixel);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_TILE_EXECUTOR_H_
