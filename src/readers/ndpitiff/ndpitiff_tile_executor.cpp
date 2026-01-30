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

#include "fastslide/readers/ndpitiff/ndpitiff_tile_executor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/ndpitiff/ndpitiff.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "simpletiff/index.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/reader.h"

namespace fastslide {
namespace {

constexpr uint16_t kCompressionJpeg = 7;

[[nodiscard]] bool LooksLikeJpegStream(std::span<const uint8_t> bytes) {
  return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
}

[[nodiscard]] uint32_t ComputeEdgeTileDim(uint32_t nominal, uint32_t offset,
                                          uint32_t full) {
  if (offset >= full) {
    return 0;
  }
  return std::min<uint32_t>(nominal, full - offset);
}

[[nodiscard]] const char* GetEnvOrNull(const char* name) {
  return std::getenv(name);
}

[[nodiscard]] bool EnvEnabled(std::string_view name) {
  const char* v = GetEnvOrNull(name.data());
  return v != nullptr && std::string_view(v) == "1";
}

[[nodiscard]] uint32_t EnvU32Or(std::string_view name, uint32_t default_value) {
  const char* v = GetEnvOrNull(name.data());
  if (v == nullptr) {
    return default_value;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(v, &end, 10);
  if (end == v) {
    return default_value;
  }
  if (parsed > std::numeric_limits<uint32_t>::max()) {
    return default_value;
  }
  return static_cast<uint32_t>(parsed);
}

struct NdpiTileTraceConfig {
  bool enabled = false;
  bool verbose = false;
  uint32_t print_every = 0;
  uint32_t slow_ms = 0;
};

[[nodiscard]] NdpiTileTraceConfig GetTraceConfig() {
  NdpiTileTraceConfig cfg;
  cfg.enabled = EnvEnabled("FASTSLIDE_NDPI_TRACE_TILES");
  cfg.verbose = EnvEnabled("FASTSLIDE_NDPI_TRACE_TILES_VERBOSE");
  cfg.print_every = EnvU32Or("FASTSLIDE_NDPI_TRACE_TILES_EVERY", 0);
  cfg.slow_ms = EnvU32Or("FASTSLIDE_NDPI_TRACE_TILES_SLOW_MS", 0);
  return cfg;
}

void TraceMaybe(const NdpiTileTraceConfig& cfg, std::mutex& log_mutex,
                const std::string& msg) {
  if (!cfg.enabled) {
    return;
  }
  std::lock_guard<std::mutex> lock(log_mutex);
  std::fprintf(stderr, "%s\n", msg.c_str());
  std::fflush(stderr);
}

}  // namespace

aifocore::Status NdpiTiffTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const NdpiTiffReader& reader,
    runtime::TileWriter& writer) {
  const int level = plan.request.level;
  if (level < 0 || level >= reader.GetLevelCount()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& tiff_index = reader.GetTiffIndex();
  const NdpiTileTraceConfig trace_cfg = GetTraceConfig();
  std::mutex log_mutex;
  std::atomic<uint64_t> completed{0};
  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
      plan, writer,
      [&](const core::TileReadOp& operation, runtime::TileWriter& writer_ref,
          std::mutex& writer_mutex) -> aifocore::Status {
        const auto t0 = std::chrono::steady_clock::now();
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
        static thread_local std::vector<uint8_t> raw_compressed;
        static thread_local std::vector<uint8_t> patched_header;

        auto& buffers = GetBuffers();
        auto& decoded_buffer = buffers.tile_buffer;

        const uint32_t tile_index =
            static_cast<uint32_t>(operation.byte_offset);

        readers::simpletiff_decode::DecodedInterleavedView decoded_view;

        const bool maybe_headerless_ndpi_jpeg =
            (page_header.storage == simpletiff::Storage::kTiles) &&
            (page_header.compression == kCompressionJpeg);

        if (maybe_headerless_ndpi_jpeg) {
          if (trace_cfg.verbose) {
            TraceMaybe(
                trace_cfg, log_mutex,
                aifocore::fmt::format(
                    "[NDPI] start page={} tile_index={} crop=({},{} {}x{})",
                    operation.source_id, tile_index,
                    operation.transform.source.x, operation.transform.source.y,
                    operation.transform.source.width,
                    operation.transform.source.height));
          }
          auto rr = simpletiff::ReadRawTile(tiff_index, operation.source_id,
                                            tile_index, raw_compressed);
          if (!rr.ok()) {
            return aifocore::Status(
                aifocore::StatusCode::kInternal,
                aifocore::fmt::format(
                    "Failed to read raw tile {} from page {}: {}", tile_index,
                    operation.source_id, rr.error().message()));
          }

          const auto raw_span = std::span<const uint8_t>(raw_compressed.data(),
                                                         raw_compressed.size());

          // If the payload already looks like a complete JPEG stream, decode it
          // directly. Otherwise, prefix with the NDPI header template and append
          // EOI.
          std::span<const uint8_t> jpeg_stream_span;
          if (LooksLikeJpegStream(raw_span)) {
            jpeg_stream_span = raw_span;
          } else {
            const auto& tiles = tiff_index.Tiles(page_header.payload_id);
            if (tiles.tiles_x == 0 || tiles.tile_w == 0 || tiles.tile_h == 0) {
              return aifocore::Status(aifocore::StatusCode::kInternal,
                                      "Invalid NDPI tile geometry");
            }
            const uint32_t tile_x = tile_index % tiles.tiles_x;
            const uint32_t tile_y = tile_index / tiles.tiles_x;
            const uint32_t px = tile_x * tiles.tile_w;
            const uint32_t py = tile_y * tiles.tile_h;
            const uint32_t actual_w =
                ComputeEdgeTileDim(tiles.tile_w, px, page_header.width);
            const uint32_t actual_h =
                ComputeEdgeTileDim(tiles.tile_h, py, page_header.height);
            if (actual_w == 0 || actual_h == 0 ||
                actual_w > std::numeric_limits<uint16_t>::max() ||
                actual_h > std::numeric_limits<uint16_t>::max()) {
              return aifocore::Status(
                  aifocore::StatusCode::kInternal,
                  aifocore::fmt::format("Invalid NDPI edge tile size {}x{}",
                                        actual_w, actual_h));
            }

            AIFOCORE_RETURN_IF_ERROR(reader.BuildPatchedJpegHeader(
                static_cast<uint16_t>(actual_w),
                static_cast<uint16_t>(actual_h), patched_header));

            decode_ctx.jpeg_stream_buffer.resize(patched_header.size() +
                                                 raw_compressed.size() + 2);
            uint8_t* dst = decode_ctx.jpeg_stream_buffer.data();
            std::memcpy(dst, patched_header.data(), patched_header.size());
            dst += patched_header.size();
            if (!raw_compressed.empty()) {
              std::memcpy(dst, raw_compressed.data(), raw_compressed.size());
              dst += raw_compressed.size();
            }
            // EOI
            dst[0] = 0xFF;
            dst[1] = 0xD9;

            jpeg_stream_span =
                std::span<const uint8_t>(decode_ctx.jpeg_stream_buffer.data(),
                                         decode_ctx.jpeg_stream_buffer.size());
          }

          const simpletiff::JpegDecodeOptions jpeg_options = {
              // If Photometric is YCbCr (6), allow conversion to RGB.
              .treat_ycbcr_as_rgb = (page_header.photometric != 6),
          };

          int decoded_w = 0;
          int decoded_h = 0;
          if (!simpletiff::DecodeJpeg(decode_ctx, jpeg_stream_span, decoded_w,
                                      decoded_h, decoded_buffer,
                                      jpeg_options)) {
            return aifocore::Status(
                aifocore::StatusCode::kInternal,
                aifocore::fmt::format(
                    "NDPI JPEG decode failed (page {} tile {})",
                    operation.source_id, tile_index));
          }

          decoded_view = readers::simpletiff_decode::DecodedInterleavedView{
              .data = std::span<const uint8_t>(decoded_buffer.data(),
                                               decoded_buffer.size()),
              .width = static_cast<uint32_t>(decoded_w),
              .height = static_cast<uint32_t>(decoded_h),
              .channels = 3,
          };
        } else {
          AIFOCORE_ASSIGN_OR_RETURN(
              decoded_view, readers::simpletiff_decode::ReadTileOrStrip(
                                tiff_index, operation.source_id, tile_index,
                                decode_ctx, decoded_buffer));
        }

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
                decoded_view.data, decoded_view.width, decoded_view.height,
                readers::simpletiff_decode::RectU32{
                    .x = src_x, .y = src_y, .width = src_w, .height = src_h},
                bytes_per_pixel, cropped_span));

        core::TileReadOp modified_op = operation;
        modified_op.transform.source.x = 0;
        modified_op.transform.source.y = 0;
        modified_op.transform.source.width = src_w;
        modified_op.transform.source.height = src_h;

        aifocore::Status st = readers::simpletiff_exec::WriteTileMaybeLocked(
            writer_ref, modified_op, cropped_view, src_w, src_h, tile_channels,
            writer_mutex);
        const auto t1 = std::chrono::steady_clock::now();
        const uint64_t done =
            completed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (trace_cfg.enabled) {
          const uint64_t total_ops = plan.operations.size();
          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0)
                  .count();
          const bool slow = (trace_cfg.slow_ms > 0 && ms >= trace_cfg.slow_ms);
          const bool periodic = (trace_cfg.print_every > 0 &&
                                 (done % trace_cfg.print_every) == 0);
          if (slow || periodic || trace_cfg.verbose) {
            TraceMaybe(
                trace_cfg, log_mutex,
                aifocore::fmt::format(
                    "[NDPI] done {}/{} page={} tile_index={} ms={} status={}",
                    done, total_ops, operation.source_id, tile_index, ms,
                    st.ok() ? "OK" : st.ToString()));
          }
        }
        return st;
      });
}

}  // namespace fastslide
