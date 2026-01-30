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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_TILE_EXECUTOR_H_

#include <limits>
#include <mutex>
#include <span>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/ometiff/ometiff.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"

namespace fastslide {

class OmetiffTileExecutor : public CachedTileExecutor<OmetiffTileExecutor> {
 public:
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const OmeTiffReader& reader,
                                      runtime::TileWriter& writer);

  friend class CachedTileExecutor<OmetiffTileExecutor>;

 private:
  struct PageState {
    const simpletiff::TiffIndex* index_identity = nullptr;
    uint32_t current_page = std::numeric_limits<uint32_t>::max();
    bool is_tiled = false;
    uint32_t tile_width = 0;
    uint32_t tile_height = 0;
    uint16_t samples_per_pixel = 1;
    uint16_t bits_per_sample = 8;
    uint32_t bytes_per_sample = 1;
  };

  static aifocore::Status ExecuteTileOperation(
      const core::TileReadOp& operation, const OmeTiffReader& reader,
      const OmeTiffLevelInfo& level_info,
      const simpletiff::TiffIndex& tiff_index, runtime::TileWriter& writer,
      std::mutex& writer_mutex, PageState& page_state);

  static aifocore::Status UpdatePageState(
      uint32_t page, const OmeTiffLevelInfo& level_info,
      const simpletiff::TiffIndex& tiff_index, PageState& page_state);

  static TileKey MakeCacheKey(const core::TileReadOp& operation,
                              const OmeTiffReader& reader,
                              const simpletiff::TiffIndex& tiff_index,
                              const PageState& page_state);

  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& operation, const OmeTiffReader& reader,
      const simpletiff::TiffIndex& tiff_index, const PageState& page_state);

  static aifocore::Result<std::span<const uint8_t>> ExtractRegionFromTile(
      const DecodedTileData& decoded_tile, const core::TileReadOp& operation,
      size_t bytes_per_pixel);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_TILE_EXECUTOR_H_
