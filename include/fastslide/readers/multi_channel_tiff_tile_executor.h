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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MULTI_CHANNEL_TIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MULTI_CHANNEL_TIFF_TILE_EXECUTOR_H_

#include <cstdint>
#include <limits>
#include <mutex>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"

/**
 * @file multi_channel_tiff_tile_executor.h
 * @brief Shared CRTP tile executor for multi-channel TIFF formats
 *        (QPTIFF, OME-TIFF).
 *
 * Both QPTIFF and OME-TIFF store one TIFF page per channel per pyramid level.
 * The runtime path (cache lookup, page-state cache, decode, paint) is
 * identical between the two formats, so it lives here as a single template
 * instantiated per format via a `using` alias.
 */

namespace fastslide {

/// @brief Generic CRTP tile executor for multi-channel, page-per-channel TIFF
///        formats.
///
/// Each pyramid level holds `LevelInfo::pages.size()` TIFF pages (one per
/// channel). The executor:
///   1. Looks up the requested tile in the reader's cache.
///   2. On miss, decodes the tile via `simpletiff_decode::ReadTileOrStrip`
///      using thread-local buffers from `TiffBasedTileExecutor`.
///   3. Paints the tile into the output canvas (with optional blend lock).
///
/// `Reader` must satisfy:
///   - `int GetLevelCount() const`
///   - `const std::vector<LevelInfo>& GetPyramid() const`
///   - `const simpletiff::TiffIndex& GetTiffIndex() const`
///   - `std::string GetFilename() const`
///   - the `SlideReader` cache interface (`IsCacheEnabled`, `GetCache`).
///
/// `LevelInfo` must expose a `.size` (`ImageDimensions`) member; its `.pages`
/// are consulted during plan building, not here.
template <typename Reader, typename LevelInfo>
class MultiChannelTiffTileExecutor
    : public CachedTileExecutor<
          MultiChannelTiffTileExecutor<Reader, LevelInfo>> {
 public:
  using Self = MultiChannelTiffTileExecutor<Reader, LevelInfo>;
  using Base = CachedTileExecutor<Self>;
  friend Base;

  /// @brief Per-page cached state (current_page, tile geometry, sample sizes).
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

  /// @brief Execute a fully-built `TilePlan` against `writer`.
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const Reader& reader,
                                      runtime::Canvas& writer) {
    const int level = plan.request.level;
    if (level < 0 || level >= reader.GetLevelCount()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Invalid level: {}", level));
    }

    const auto& pyramid = reader.GetPyramid();
    const auto& tiff_index = reader.GetTiffIndex();
    const LevelInfo& level_info = pyramid[level];

    return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
        plan, writer,
        [&](const core::TileReadOp& operation, runtime::Canvas& writer_ref,
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

  // ---- CachedTileExecutor CRTP hooks (must be public for the base) ----

  static runtime::TileKey MakeCacheKey(const core::TileReadOp& operation,
                                       const Reader& reader,
                                       const simpletiff::TiffIndex& tiff_index,
                                       const PageState& page_state) {
    (void)tiff_index;
    (void)page_state;
    return runtime::TileKey(reader.GetFilename(), operation.source_id,
                            static_cast<uint32_t>(operation.byte_offset), 0);
  }

  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& operation, const Reader& reader,
      const simpletiff::TiffIndex& tiff_index, const PageState& page_state) {
    (void)reader;
    auto& tile_buffer = Base::GetBuffers().tile_buffer;
    static thread_local simpletiff::DecodeContext decode_ctx;

    const uint32_t page_index = page_state.current_page;
    const uint32_t tile_or_strip_index =
        static_cast<uint32_t>(operation.byte_offset);

    AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                              readers::simpletiff_decode::ReadTileOrStrip(
                                  tiff_index, page_index, tile_or_strip_index,
                                  decode_ctx, tile_buffer));
    return DecodedTileData{decoded.data, decoded.width, decoded.height,
                           decoded.channels};
  }

 private:
  static aifocore::Status ExecuteTileOperation(
      const core::TileReadOp& operation, const Reader& reader,
      const LevelInfo& level_info, const simpletiff::TiffIndex& tiff_index,
      runtime::Canvas& writer, std::mutex& writer_mutex,
      PageState& page_state) {
    const uint32_t page = operation.source_id;

    AIFOCORE_RETURN_IF_ERROR(
        UpdatePageState(page, level_info, tiff_index, page_state));

    AIFOCORE_ASSIGN_OR_RETURN(
        const auto decoded_tile,
        Base::ReadWithCacheDecoded(operation, reader, tiff_index, page_state));
    return readers::simpletiff_exec::PaintTileMaybeLocked(
        writer, operation, decoded_tile.data, decoded_tile.width,
        decoded_tile.height, decoded_tile.channels, writer_mutex);
  }

  static aifocore::Status UpdatePageState(
      uint32_t page, const LevelInfo& level_info,
      const simpletiff::TiffIndex& tiff_index, PageState& page_state) {
    if (page == page_state.current_page) {
      return aifocore::Status::OkStatus();
    }
    if (page >= tiff_index.NumPages()) {
      return AIFOCORE_MAKE_STATUS(
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
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MULTI_CHANNEL_TIFF_TILE_EXECUTOR_H_
