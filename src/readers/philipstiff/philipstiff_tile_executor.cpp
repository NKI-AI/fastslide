// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#include "fastslide/readers/philipstiff/philipstiff_tile_executor.h"

#include <mutex>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/philipstiff/philipstiff.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status PhilipsTiffTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const PhilipsTiffReader& reader,
    runtime::Canvas& writer) {
  const int level = plan.request.level;
  if (level < 0 || level >= reader.GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& tiff_index = reader.GetTiffIndex();
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

        static thread_local simpletiff::DecodeContext decode_ctx;
        auto& buffers = GetBuffers();

        auto& decoded_buffer = buffers.tile_buffer;
        const uint32_t tile_index =
            static_cast<uint32_t>(operation.byte_offset);

        AIFOCORE_ASSIGN_OR_RETURN(
            auto decoded, readers::simpletiff_decode::ReadTileOrStrip(
                              tiff_index, operation.source_id, tile_index,
                              decode_ctx, decoded_buffer));

        return readers::simpletiff_exec::PaintTileMaybeLocked(
            writer_ref, operation, decoded.data, decoded.width, decoded.height,
            tile_channels, writer_mutex);
      });
}

}  // namespace fastslide
