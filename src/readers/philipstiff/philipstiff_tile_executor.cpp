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
#include "fastslide/readers/philipstiff/philipstiff_tile_executor.h"

#include <mutex>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {

aifocore::Status PhilipsTiffTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const PhilipsTiffExecContext& context,
    runtime::Canvas& writer) {
  const int level = plan.request.level;
  if (level < 0 || level >= context.level_count) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& tiff_index = context.tiff_index;
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
