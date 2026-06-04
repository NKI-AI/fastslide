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

#include "fastslide/readers/bif/bif_tile_executor.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide {

namespace {

// Returns true if the referenced tile has no on-disk data (unscanned hole).
[[nodiscard]] bool IsUnscannedTile(const simpletiff::TiffIndex& tiff,
                                   uint32_t page_index, uint32_t tile_index) {
  if (page_index >= tiff.NumPages()) {
    return false;
  }
  const auto& page = tiff.Page(page_index);
  if (page.storage != simpletiff::Storage::kTiles) {
    return false;
  }
  const auto& tiles = tiff.Tiles(page.payload_id);

  // BigTIFF pyramid pages often keep TileByteCounts lazy (only the count is
  // known at parse time). Indexing the bytecounts arena before it is loaded
  // reads out of bounds and crashes.
  if (!tiles.lazy_bytecounts.loaded) {
    uint64_t offset = 0;
    uint64_t bytecount = 0;
    if (!simpletiff::EnsureTileLoaded(tiff, page_index, tile_index, offset,
                                      bytecount)) {
      return false;
    }
    return bytecount == 0;
  }

  if (tiles.bytecounts.count == 0) {
    return false;
  }
  const auto byte_counts = tiff.Bytecounts(tiles.bytecounts);
  return tile_index < byte_counts.size() && byte_counts[tile_index] == 0;
}

}  // namespace

aifocore::Status BifTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                              const BifExecContext& context,
                                              runtime::Canvas& writer) {
  if (plan.operations.empty()) {
    const auto& background = plan.output.background;
    return writer.FillBackground(background.r, background.g, background.b);
  }

  // Group operations by physical tile (source IFD + linear tile index). At
  // higher pyramid levels a single physical tile is the source for d*d level-0
  // sub-rect crops; decoding it once per physical tile (instead of once per
  // sub-rect) avoids thousands of redundant JPEG decodes of the same tile.
  std::unordered_map<uint64_t, uint32_t>
      group_of;                  // physical key -> decode idx
  std::vector<uint32_t> rep_op;  // one representative op per physical tile
  group_of.reserve(plan.operations.size());
  std::vector<uint32_t> op_group(plan.operations.size());
  for (uint32_t i = 0; i < plan.operations.size(); ++i) {
    const auto& op = plan.operations[i];
    const uint64_t key = (static_cast<uint64_t>(op.source_id) << 32) |
                         static_cast<uint32_t>(op.byte_offset);
    auto [it, inserted] =
        group_of.try_emplace(key, static_cast<uint32_t>(rep_op.size()));
    if (inserted) {
      rep_op.push_back(i);
    }
    op_group[i] = it->second;
  }

  const auto& tiff = context.Tiff();

  // Phase 1: decode each unique physical tile once, in parallel, into an owned
  // buffer. Decoding is the expensive step (JPEG/YCbCr) and is embarrassingly
  // parallel; painting (phase 2) must stay serial and ordered, so the decoded
  // pixels are copied out of the thread-local/cached decode buffers here.
  struct OwnedTile {
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    bool paintable = false;  // decoded and non-empty (not unscanned)
  };

  std::vector<OwnedTile> decoded(rep_op.size());

  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex error_mutex;
  std::optional<aifocore::Status> first_error;
  std::atomic<bool> has_error{false};
  const auto record_error = [&](const aifocore::Status& status) {
    has_error.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(error_mutex);
    if (!first_error.has_value()) {
      first_error = status;
    }
  };

  auto futures = pool.submit_sequence(size_t{0}, rep_op.size(), [&](size_t gi) {
    if (has_error.load(std::memory_order_relaxed)) {
      return;
    }
    const core::TileReadOp& first = plan.operations[rep_op[gi]];
    if (first.source_id >= tiff.NumPages()) {
      record_error(AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("BIF page {} out of range", first.source_id)));
      return;
    }
    const auto tile_index = static_cast<uint32_t>(first.byte_offset);

    // Unscanned tiles have no data: leave the (ScanWhitePoint) background.
    if (IsUnscannedTile(tiff, first.source_id, tile_index)) {
      return;
    }

    auto decoded_or = ReadWithCacheDecoded(first, context);
    if (!decoded_or.ok()) {
      record_error(decoded_or.status());
      return;
    }
    const DecodedTileData& src = *decoded_or;
    OwnedTile& dst = decoded[gi];
    dst.width = src.width;
    dst.height = src.height;
    dst.channels = src.channels;
    dst.data.assign(src.data.begin(), src.data.end());
    dst.paintable = !dst.data.empty();
  });
  futures.wait();
  if (first_error.has_value()) {
    return *first_error;
  }

  // BIF tiles overlap their neighbours. Overlaps resolve as last-writer-wins in
  // raster order, so the right/bottom tile is visible in the shared band. The
  // canvas coverage map is first-writer-wins, so paint *every* sub-tile op in
  // reverse raster order (bottom-right first): the bottom-right tile claims the
  // overlap and the top-left tile is clipped against it. This must order all
  // ops globally, not just whole physical tiles - at higher levels a single
  // physical tile hosts d*d overlapping level-0 sub-tiles whose mutual overlap
  // must also resolve bottom-right-wins, otherwise per-sub-tile seams appear.
  std::vector<uint32_t> paint_order(plan.operations.size());
  for (uint32_t i = 0; i < paint_order.size(); ++i) {
    paint_order[i] = i;
  }
  std::sort(paint_order.begin(), paint_order.end(),
            [&plan](uint32_t a, uint32_t b) {
              const auto& da = plan.operations[a].transform.dest;
              const auto& db = plan.operations[b].transform.dest;
              if (da.y != db.y) {
                return da.y > db.y;
              }
              return da.x > db.x;
            });

  // Phase 2: paint in global reverse raster order.
  std::mutex writer_mutex;
  for (uint32_t op_idx : paint_order) {
    const OwnedTile& tile = decoded[op_group[op_idx]];
    if (!tile.paintable) {
      continue;
    }
    const DecodedTileData view{
        .data = std::span<const uint8_t>(tile.data),
        .width = tile.width,
        .height = tile.height,
        .channels = tile.channels,
    };
    AIFOCORE_RETURN_IF_ERROR(
        PaintOp(plan.operations[op_idx], view, writer, writer_mutex));
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status BifTileExecutor::PaintOp(const core::TileReadOp& op,
                                          const DecodedTileData& decoded,
                                          runtime::Canvas& writer,
                                          std::mutex& writer_mutex) {
  // Clamp the source sub-rectangle to the decoded tile (edge tiles in higher
  // pyramid levels can be shorter/narrower than the nominal tile size). The
  // transform has scale 1, so the destination shrinks with the source.
  core::TileReadOp clamped = op;
  const auto src_x = static_cast<uint32_t>(clamped.transform.source.x);
  const auto src_y = static_cast<uint32_t>(clamped.transform.source.y);
  if (src_x >= decoded.width || src_y >= decoded.height) {
    return aifocore::Status::OkStatus();
  }
  const uint32_t max_w = decoded.width - src_x;
  const uint32_t max_h = decoded.height - src_y;
  const uint32_t clamped_w =
      std::min<uint32_t>(clamped.transform.source.width, max_w);
  const uint32_t clamped_h =
      std::min<uint32_t>(clamped.transform.source.height, max_h);
  if (clamped_w == 0 || clamped_h == 0) {
    return aifocore::Status::OkStatus();
  }
  clamped.transform.source.width = clamped_w;
  clamped.transform.source.height = clamped_h;
  clamped.transform.dest.width = clamped_w;
  clamped.transform.dest.height = clamped_h;

  return readers::simpletiff_exec::PaintTileMaybeLocked(
      writer, clamped, decoded.data, decoded.width, decoded.height,
      decoded.channels, writer_mutex);
}

TileKey BifTileExecutor::MakeCacheKey(const core::TileReadOp& op,
                                      const BifExecContext& context) {
  // The decoded physical tile is identified by (file, source IFD, tile index);
  // the same tile is reused for multiple sub-rect crops at higher levels.
  return runtime::TileKey(context.GetFilename(),
                          static_cast<uint16_t>(op.source_id),
                          static_cast<uint32_t>(op.byte_offset), 0U);
}

aifocore::Result<DecodedTileData> BifTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const BifExecContext& context) {
  const auto& tiff = context.Tiff();
  static thread_local simpletiff::DecodeContext decode_ctx;
  auto& decoded_buffer = GetBuffers().tile_buffer;

  AIFOCORE_ASSIGN_OR_RETURN(
      auto view, readers::simpletiff_decode::ReadTileOrStrip(
                     tiff, op.source_id, static_cast<uint32_t>(op.byte_offset),
                     decode_ctx, decoded_buffer));
  return DecodedTileData{
      .data = view.data,
      .width = view.width,
      .height = view.height,
      .channels = view.channels,
  };
}

}  // namespace fastslide
