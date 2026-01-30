// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#include "fastslide/readers/philipstiff/philipstiff_tile_executor.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/philipstiff/philipstiff.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status PhilipsTiffTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const PhilipsTiffReader& reader,
    runtime::TileWriter& writer) {
  const int level = plan.request.level;
  if (level < 0 || level >= reader.GetLevelCount()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& tiff_index = reader.GetTiffIndex();
  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
      plan, writer,
      [&](const core::TileReadOp& operation, runtime::TileWriter& writer_ref,
          std::mutex& writer_mutex) -> aifocore::Status {
        if (operation.source_id >= tiff_index.NumPages()) {
          return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                                  aifocore::fmt::format("Page {} out of range",
                                                        operation.source_id));
        }

        const auto& page_header = tiff_index.Page(operation.source_id);
        const uint32_t tile_channels =
            static_cast<uint32_t>(page_header.samples_per_pixel);
        const size_t bytes_per_sample =
            static_cast<size_t>(readers::simpletiff_plan::BytesPerSample(
                page_header.bits_per_sample));
        const size_t bytes_per_pixel =
            bytes_per_sample * static_cast<size_t>(tile_channels);

        static thread_local simpletiff::DecodeContext decode_ctx;
        auto& buffers = GetBuffers();

        auto& decoded_buffer = buffers.tile_buffer;
        const uint32_t tile_index =
            static_cast<uint32_t>(operation.byte_offset);

        AIFOCORE_ASSIGN_OR_RETURN(
            auto decoded, readers::simpletiff_decode::ReadTileOrStrip(
                              tiff_index, operation.source_id, tile_index,
                              decode_ctx, decoded_buffer));

        const uint32_t src_x = operation.transform.source.x;
        const uint32_t src_y = operation.transform.source.y;
        const uint32_t src_w = operation.transform.source.width;
        const uint32_t src_h = operation.transform.source.height;

        const size_t crop_bytes =
            static_cast<size_t>(src_w) * src_h * bytes_per_pixel;
        uint8_t* cropped_ptr = buffers.GetCropBuffer(crop_bytes);
        const auto cropped_span = std::span<uint8_t>(cropped_ptr, crop_bytes);
        AIFOCORE_ASSIGN_OR_RETURN(
            auto cropped_view,
            readers::simpletiff_decode::CropInterleavedRoi(
                decoded.data, decoded.width, decoded.height,
                readers::simpletiff_decode::RectU32{
                    .x = src_x, .y = src_y, .width = src_w, .height = src_h},
                bytes_per_pixel, cropped_span));

        core::TileReadOp modified_op = operation;
        modified_op.transform.source.x = 0;
        modified_op.transform.source.y = 0;
        modified_op.transform.source.width = src_w;
        modified_op.transform.source.height = src_h;

        return readers::simpletiff_exec::WriteTileMaybeLocked(
            writer_ref, modified_op, cropped_view, src_w, src_h, tile_channels,
            writer_mutex);
      });
}

}  // namespace fastslide
