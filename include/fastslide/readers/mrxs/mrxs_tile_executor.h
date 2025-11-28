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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_TILE_EXECUTOR_H_

#include <vector>

#include "aifocore/status/result.h"
#include <mutex>
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

// Forward declaration
class MrxsReader;

/// @brief Helper class for executing MRXS tile read operations
class MrxsTileExecutor {
public:
  /// @brief Execute a tile plan
  /// @param plan The tile plan to execute
  /// @param reader MRXS reader instance
  /// @param writer Tile writer for output
  /// @return Status indicating success or failure
  static aifocore::Status ExecutePlan(const core::TilePlan &plan,
                                      const MrxsReader &reader,
                                      runtime::TileWriter &writer);

private:
  /// @brief Execute a single tile operation
  /// @param op The tile operation to execute
  /// @param reader MRXS reader instance
  /// @param zoom_level Zoom level metadata
  /// @param writer Tile writer
  /// @param accumulator_mutex Mutex for thread-safe accumulation
  /// @return Status indicating success or failure
  static aifocore::Status
  ExecuteTileOperation(const core::TileReadOp &op, const MrxsReader &reader,
                       const mrxs::SlideZoomLevel &zoom_level,
                       runtime::TileWriter &writer,
                       std::mutex &accumulator_mutex);

  /// @brief Read and decode a single tile
  /// @param op The tile operation
  /// @param reader MRXS reader instance
  /// @param zoom_level Zoom level metadata
  /// @return Decoded RGB image or error status
  static aifocore::Result<RGBImage>
  ReadAndDecodeTile(const core::TileReadOp &op, const MrxsReader &reader,
                    const mrxs::SlideZoomLevel &zoom_level);

  /// @brief Extract sub-region from decoded tile
  /// @param image Decoded tile image
  /// @param op Tile operation with crop information
  /// @return Extracted sub-region
  static RGBImage ExtractSubRegion(const RGBImage &image,
                                   const core::TileReadOp &op);

  /// @brief Check if sub-region extraction is needed
  /// @param image_width Decoded image width
  /// @param image_height Decoded image height
  /// @param expected_width Expected output width
  /// @param expected_height Expected output height
  /// @return True if extraction is needed
  static bool NeedsSubRegionExtraction(uint32_t image_width,
                                       uint32_t image_height,
                                       uint32_t expected_width,
                                       uint32_t expected_height);
};

} // namespace fastslide

#endif // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_TILE_EXECUTOR_H_
