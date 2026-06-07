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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/ndpitiff/ndpitiff_jpeg_header.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/runtime/decoders/jpeg_xr_decoder.h"
#include "simpletiff/index.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/reader.h"

namespace fastslide {
namespace {

constexpr uint16_t kCompressionJpeg = 7;
constexpr uint16_t kCompressionJpegXr = 22610;
constexpr uint16_t kPhotometricYCbCr = 6;

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

}  // namespace

aifocore::Status NdpiTiffTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const NdpiTiffExecContext& context,
    runtime::Canvas& writer) {
  const int level = plan.request.level;
  if (level < 0 || level >= context.GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
      plan, writer,
      [&](const core::TileReadOp& op, runtime::Canvas& writer_ref,
          std::mutex& writer_mutex) -> aifocore::Status {
        return ExecuteTileOperation(op, context, writer_ref, writer_mutex);
      });
}

aifocore::Status NdpiTiffTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const NdpiTiffExecContext& context,
    runtime::Canvas& writer, std::mutex& writer_mutex) {
  auto decoded_or = ReadWithCacheDecoded(op, context);
  if (!decoded_or.ok()) {
    return decoded_or.status();
  }
  const DecodedTileData& decoded = *decoded_or;

  return readers::simpletiff_exec::PaintTileMaybeLocked(
      writer, op, decoded.data, decoded.width, decoded.height, decoded.channels,
      writer_mutex);
}

TileKey NdpiTiffTileExecutor::MakeCacheKey(const core::TileReadOp& op,
                                           const NdpiTiffExecContext& context) {
  // Key by the TIFF page (`source_id`) rather than the pyramid level: in
  // NDPI z-stacks several focal planes share the same (level, tile) grid, so
  // the page index is what uniquely identifies a decoded tile across focal
  // planes and levels.
  return runtime::TileKey(context.GetFilename(),
                          static_cast<uint16_t>(op.source_id), op.tile_coord.x,
                          op.tile_coord.y);
}

aifocore::Result<DecodedTileData> NdpiTiffTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const NdpiTiffExecContext& context) {
  const auto& tiff_index = context.GetTiffIndex();
  if (op.source_id >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Page {} out of range", op.source_id));
  }

  const auto& page_header = tiff_index.Page(op.source_id);
  const uint32_t tile_channels =
      static_cast<uint32_t>(page_header.samples_per_pixel);

  static thread_local simpletiff::DecodeContext decode_ctx;
  static thread_local std::vector<uint8_t> raw_compressed;
  static thread_local std::vector<uint8_t> patched_header;

  auto& buffers = GetBuffers();
  auto& decoded_buffer = buffers.tile_buffer;

  const uint32_t tile_index = static_cast<uint32_t>(op.byte_offset);

  // JPEG XR (NDPI compression 22610): each tile/strip is a complete,
  // self-contained JXR stream (unlike NDPI JPEG, which shares a header
  // template). Read the raw payload and decode it directly with jxrlib.
  if (page_header.compression == kCompressionJpegXr) {
    auto rr = simpletiff::ReadRawTile(tiff_index, op.source_id, tile_index,
                                      raw_compressed);
    if (!rr.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format(
              "Failed to read raw JPEG XR tile {} from page {}: {}", tile_index,
              op.source_id, rr.error().message()));
    }

    const auto raw_span =
        std::span<const uint8_t>(raw_compressed.data(), raw_compressed.size());
    AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                              runtime::decoders::DecodeJpegXrToRgb(raw_span));

    decoded_buffer.assign(decoded.rgb.begin(), decoded.rgb.end());
    return DecodedTileData{
        .data = std::span<const uint8_t>(decoded_buffer.data(),
                                         decoded_buffer.size()),
        .width = decoded.width,
        .height = decoded.height,
        .channels = 3,
    };
  }

  const bool maybe_headerless_ndpi_jpeg =
      (page_header.storage == simpletiff::Storage::kTiles) &&
      (page_header.compression == kCompressionJpeg);

  if (!maybe_headerless_ndpi_jpeg) {
    AIFOCORE_ASSIGN_OR_RETURN(
        auto decoded_view,
        readers::simpletiff_decode::ReadTileOrStrip(
            tiff_index, op.source_id, tile_index, decode_ctx, decoded_buffer));
    return DecodedTileData{
        .data = decoded_view.data,
        .width = decoded_view.width,
        .height = decoded_view.height,
        .channels = decoded_view.channels,
    };
  }

  // Headerless NDPI JPEG path: each tile stores raw entropy-coded scan data
  // sharing a JPEG header template with the rest of the level.
  auto rr = simpletiff::ReadRawTile(tiff_index, op.source_id, tile_index,
                                    raw_compressed);
  if (!rr.ok()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read raw tile {} from page {}: {}",
                              tile_index, op.source_id, rr.error().message()));
  }

  const auto raw_span =
      std::span<const uint8_t>(raw_compressed.data(), raw_compressed.size());

  std::span<const uint8_t> jpeg_stream_span;
  if (LooksLikeJpegStream(raw_span)) {
    jpeg_stream_span = raw_span;
  } else {
    const auto& tiles = tiff_index.Tiles(page_header.payload_id);
    if (tiles.tiles_x == 0 || tiles.tile_w == 0 || tiles.tile_h == 0) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
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
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format("Invalid NDPI edge tile size {}x{}", actual_w,
                                actual_h));
    }

    AIFOCORE_RETURN_IF_ERROR(BuildPatchedNdpiJpegHeader(
        context.GetJpegHeaderTemplate(), context.GetSofHeightOffsets(),
        context.GetSofWidthOffsets(), static_cast<uint16_t>(actual_w),
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
      // If Photometric is YCbCr (6), let the JPEG decoder convert to RGB.
      .treat_ycbcr_as_rgb = (page_header.photometric != kPhotometricYCbCr),
  };

  int decoded_w = 0;
  int decoded_h = 0;
  if (!simpletiff::DecodeJpeg(decode_ctx, jpeg_stream_span, decoded_w,
                              decoded_h, decoded_buffer, jpeg_options)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("NDPI JPEG decode failed (page {} tile {})",
                              op.source_id, tile_index));
  }

  return DecodedTileData{
      .data = std::span<const uint8_t>(decoded_buffer.data(),
                                       decoded_buffer.size()),
      .width = static_cast<uint32_t>(decoded_w),
      .height = static_cast<uint32_t>(decoded_h),
      .channels = tile_channels,
  };
}

}  // namespace fastslide
