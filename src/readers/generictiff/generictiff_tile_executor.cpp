// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#include "fastslide/readers/generictiff/generictiff_tile_executor.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "simpletiff/reader.h"

namespace fastslide {

namespace {

[[nodiscard]] bool IsFastSlideDebugEnabled() {
  static bool checked = false;
  static bool enabled = false;
  if (!checked) {
    const char* env = std::getenv("FASTSLIDE_DEBUG");
    enabled = (env != nullptr && std::strcmp(env, "1") == 0);
    checked = true;
  }
  return enabled;
}

}  // namespace

aifocore::Status GenericTiffTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const GenericTiffExecContext& context,
    runtime::Canvas& writer) {
  const int level = plan.request.level;
  if (level < 0 || level >= context.level_count) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& tiff_index = context.tiff_index;
  if (IsFastSlideDebugEnabled()) {
    std::cerr << "[GenericTIFF] ExecutePlan: ops=" << plan.operations.size()
              << " level=" << level << " out=(" << plan.output.dimensions[0]
              << "x" << plan.output.dimensions[1]
              << ") channels=" << plan.output.channels << "\n";
  }

  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
      plan, writer,
      [&](const core::TileReadOp& operation, runtime::Canvas& writer_ref,
          std::mutex& writer_mutex) -> aifocore::Status {
        if (operation.source_id >= tiff_index.NumPages()) {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kInvalidArgument,
              aifocore::fmt::format("Page {} out of range",
                                    operation.source_id));
        }

        const auto& page_header = tiff_index.Page(operation.source_id);
        const uint32_t tile_channels =
            static_cast<uint32_t>(page_header.samples_per_pixel);

        if (IsFastSlideDebugEnabled()) {
          static std::atomic<uint32_t> debug_ops{0};
          const uint32_t op_index =
              debug_ops.fetch_add(1, std::memory_order_relaxed);
          if (op_index < 10) {
            std::cerr << "[GenericTIFF] ReadTile/Strip page="
                      << operation.source_id
                      << " bits=" << page_header.bits_per_sample
                      << " spp=" << page_header.samples_per_pixel
                      << " storage=" << static_cast<int>(page_header.storage)
                      << " idx=" << operation.byte_offset << " crop=("
                      << operation.transform.source.x << ","
                      << operation.transform.source.y << " "
                      << operation.transform.source.width << "x"
                      << operation.transform.source.height << ")"
                      << " storage=" << static_cast<int>(page_header.storage)
                      << " compression=" << page_header.compression
                      << " photometric=" << page_header.photometric << "\n";
          }
        }

        AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                                  ReadWithCacheDecoded(operation, context));

        return readers::simpletiff_exec::PaintTileMaybeLocked(
            writer_ref, operation, decoded.data, decoded.width, decoded.height,
            tile_channels, writer_mutex);
      });
}

runtime::TileKey GenericTiffTileExecutor::MakeCacheKey(
    const core::TileReadOp& operation, const GenericTiffExecContext& context) {
  // The page identifies the level (or plane) and byte_offset is the linear
  // tile index (tiled pages) or strip index (striped pages).
  return runtime::TileKey(context.GetFilename(),
                          static_cast<uint16_t>(operation.source_id),
                          static_cast<uint32_t>(operation.byte_offset), 0);
}

aifocore::Result<DecodedTileData> GenericTiffTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& operation, const GenericTiffExecContext& context) {
  static thread_local simpletiff::DecodeContext decode_ctx;
  auto& decoded_buffer = GetBuffers().tile_buffer;
  AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                            readers::simpletiff_decode::ReadTileOrStrip(
                                context.tiff_index, operation.source_id,
                                static_cast<uint32_t>(operation.byte_offset),
                                decode_ctx, decoded_buffer));
  return DecodedTileData{decoded.data, decoded.width, decoded.height,
                         decoded.channels};
}

}  // namespace fastslide
