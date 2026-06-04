// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#include "fastslide/readers/olympusvsi/olympusvsi_tile_executor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/olympusvsi/olympusvsi_ets.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/decoders/j2k_decoder.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/runtime/io/file_reader.h"

namespace fastslide::formats::olympusvsi {

namespace {

/// @brief Read `n_bytes` from `file` starting at `offset` into `out`.
aifocore::Status PreadBytes(FileReader& file, uint64_t offset, uint32_t n_bytes,
                            std::vector<uint8_t>* out) {
  AIFOCORE_RETURN_IF_ERROR(file.Seek(static_cast<int64_t>(offset)));
  out->resize(n_bytes);
  if (n_bytes > 0) {
    AIFOCORE_RETURN_IF_ERROR(file.Read(out->data(), n_bytes));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Result<runtime::decoders::DecodedRgb> DecodeTileBytes(
    std::span<const uint8_t> bytes, TileCodec declared) {
  TileCodec codec =
      SniffCodec(bytes.subspan(0, std::min<size_t>(bytes.size(), 16)));
  if (codec == TileCodec::kUnknown) {
    codec = declared;
  }
  switch (codec) {
    case TileCodec::kJpeg:
      return runtime::decoders::DecodeJpegToRgb(bytes);
    case TileCodec::kJp2:
      return runtime::decoders::DecodeJ2kToRgb(bytes);
    case TileCodec::kUnknown:
    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format(
              "Olympus VSI: unsupported tile codec '{}' (declared '{}')",
              CodecName(codec), CodecName(declared)));
  }
}

/// @brief 16-bit decode path. Only JPEG 2000 is supported for the
///        ``USHORT`` pixel type; baseline JPEG never carries 16-bit
///        Olympus tiles in the validated samples.
///
/// Each fluorescence tile is an independent grayscale plane (1
/// component); 16-bit colour tiles (3 components) are also accepted.
/// The per-tile sample count is decoupled from the image-level channel
/// count: stacked grayscale planes are separate single-component tiles
/// painted into distinct output channels by the caller.
aifocore::Result<runtime::decoders::DecodedRgb16> DecodeTileBytes16(
    std::span<const uint8_t> bytes, TileCodec declared) {
  TileCodec codec =
      SniffCodec(bytes.subspan(0, std::min<size_t>(bytes.size(), 16)));
  if (codec == TileCodec::kUnknown) {
    codec = declared;
  }
  if (codec != TileCodec::kJp2) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format(
            "Olympus VSI: 16-bit tiles must be JPEG2000-encoded "
            "(saw codec '{}', declared '{}')",
            CodecName(codec), CodecName(declared)));
  }
  return runtime::decoders::DecodeJ2k16(bytes);
}

}  // namespace

aifocore::Status OlympusVsiTileExecutor::ExecutePlan(
    const core::TilePlan& plan, const OlympusVsiExecContext& context,
    runtime::Canvas& canvas) {
  const int level_index = plan.request.level;
  if (level_index < 0 ||
      static_cast<size_t>(level_index) >= context.pyramid.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Olympus VSI: invalid level {}", level_index));
  }
  const std::string ets_path_str(context.ets_path);
  const auto cache = context.cache;
  const bool is_16bit = context.pixel_type == TilePixelType::kUInt16;
  // Stacked grayscale fluorescence (more than one plane) paints into a
  // separate-planar Canvas; each tile is one mono plane routed to the
  // op's destination channel. Single-plane stacks (brightfield RGB or
  // single-band fluorescence) paint straight through.
  const bool separate_planar = is_16bit && context.n_channels > 1U;

  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
      plan, canvas,
      [&](const core::TileReadOp& op, runtime::Canvas& writer,
          std::mutex& writer_mutex) -> aifocore::Status {
        // For the separate-planar path ``PaintTilePlanar`` reads the
        // destination channel from ``tile_coord.x``; the plan builder
        // carried it in ``channel_group_offset``. Build a paint-op copy
        // with that substitution. The contiguous path ignores
        // ``tile_coord`` entirely (it paints by ``transform``).
        core::TileReadOp paint_op = op;
        if (separate_planar) {
          paint_op.tile_coord = {op.channel_group_offset, 0U};
        }

        // Cache key is the physical tile byte offset, which is unique
        // per (channel, x, y) plane — so different channels sharing an
        // (x, y) grid cell never collide.
        runtime::TileKey key(
            ets_path_str, static_cast<uint16_t>(level_index),
            static_cast<uint32_t>(op.byte_offset & 0xFFFFFFFFU),
            static_cast<uint32_t>(op.byte_offset >> 32));
        std::shared_ptr<runtime::CachedTileData> cached;
        if (cache) {
          cached = cache->Get(key);
        }
        if (cached) {
          return readers::simpletiff_exec::PaintTileMaybeLocked(
              writer, paint_op,
              std::span<const uint8_t>(cached->data.data(),
                                       cached->data.size()),
              cached->size[0], cached->size[1], cached->channels, writer_mutex);
        }

        // Read compressed tile bytes from disk. A fresh FILE* per worker
        // avoids seek-pointer contention between concurrent decodes.
        thread_local std::vector<uint8_t> compressed;
        FileReader file;
        AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(ets_path_str, "rb"));
        AIFOCORE_RETURN_IF_ERROR(
            PreadBytes(file, op.byte_offset, op.byte_size, &compressed));
        const std::span<const uint8_t> compressed_span(compressed.data(),
                                                       compressed.size());

        // Dispatch on declared pixel type:
        //   * uint8 → DecodeJpegToRgb / DecodeJ2kToRgb → 3-channel RGB8
        //     (the classic brightfield path).
        //   * uint16 → DecodeJ2k16 → 1 (grayscale plane) or 3 (16-bit
        //     colour) interleaved uint16 samples.
        // The Canvas is already configured by the plan builder to match
        // (kUInt8/kUInt16, contiguous or separate planar), so we pass the
        // raw decoded bytes through.
        uint32_t tile_w = 0;
        uint32_t tile_h = 0;
        uint32_t tile_channels = 0;
        std::vector<uint8_t> tile_bytes;
        if (context.declared_codec == TileCodec::kRaw) {
          // RAW: the payload is uncompressed pixel data laid out as
          // ``tile_w x tile_h x components`` in the declared pixel type
          // (little-endian on disk, consumed in host endianness like the
          // decoder outputs). Geometry comes from the level header; the
          // tile carries no self-describing dimensions.
          const auto& lvl = context.pyramid[static_cast<size_t>(level_index)];
          tile_w = lvl.tile_w;
          tile_h = lvl.tile_h;
          const uint32_t bytes_per_sample = is_16bit ? 2U : 1U;
          // Each separate-planar fluorescence tile holds one component; all
          // other layouts interleave ``n_channels`` components per pixel.
          tile_channels =
              separate_planar ? 1U : std::max(1U, context.n_channels);
          const size_t expected = static_cast<size_t>(tile_w) * tile_h *
                                  tile_channels * bytes_per_sample;
          if (compressed.size() < expected) {
            return AIFOCORE_MAKE_STATUS(
                aifocore::StatusCode::kInvalidArgument,
                aifocore::fmt::format(
                    "Olympus VSI: RAW tile too small: {} bytes < expected {} "
                    "({}x{}x{} @ {} B/sample)",
                    compressed.size(), expected, tile_w, tile_h, tile_channels,
                    bytes_per_sample));
          }
          tile_bytes.assign(
              compressed.begin(),
              compressed.begin() + static_cast<std::ptrdiff_t>(expected));
        } else if (!is_16bit) {
          AIFOCORE_ASSIGN_OR_RETURN(
              auto decoded,
              DecodeTileBytes(compressed_span, context.declared_codec));
          tile_w = decoded.width;
          tile_h = decoded.height;
          tile_channels = 3U;
          tile_bytes = std::move(decoded.rgb);
        } else {
          AIFOCORE_ASSIGN_OR_RETURN(
              auto decoded,
              DecodeTileBytes16(compressed_span, context.declared_codec));
          if (separate_planar && decoded.channels != 1U) {
            return AIFOCORE_MAKE_STATUS(
                aifocore::StatusCode::kUnimplemented,
                aifocore::fmt::format(
                    "Olympus VSI: stacked 16-bit fluorescence expects "
                    "single-component tiles, decoded {} components",
                    decoded.channels));
          }
          tile_w = decoded.width;
          tile_h = decoded.height;
          tile_channels = decoded.channels;
          // ``DecodedRgb16::rgb`` is a ``std::vector<uint16_t>``; rebind
          // to a flat byte buffer for the Canvas paint path. The Canvas
          // is configured for ``kUInt16`` so it consumes 2 bytes per
          // sample in host endianness, matching the decoder output.
          const size_t byte_count = decoded.rgb.size() * sizeof(uint16_t);
          tile_bytes.resize(byte_count);
          if (byte_count > 0) {
            std::memcpy(tile_bytes.data(), decoded.rgb.data(), byte_count);
          }
        }

        // Sanity check: the decoded tile must match the level's declared
        // tile geometry. Border tiles legitimately come back as
        // ``tile_w x tile_h`` (the encoder pads them), so we accept
        // anything >= the requested source crop instead of strictly
        // equal.
        const uint32_t src_w = op.transform.source.width;
        const uint32_t src_h = op.transform.source.height;
        if (tile_w < op.transform.source.x + src_w ||
            tile_h < op.transform.source.y + src_h) {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kInternal,
              aifocore::fmt::format(
                  "Olympus VSI: decoded tile {}x{} too small for crop "
                  "({},{},{}x{})",
                  tile_w, tile_h, static_cast<int>(op.transform.source.x),
                  static_cast<int>(op.transform.source.y), src_w, src_h));
        }

        // Store the FULL decoded tile (not just the crop) in the cache
        // so future reads that overlap with this tile can paint a
        // different crop without re-decoding.
        if (cache) {
          auto entry = std::make_shared<runtime::CachedTileData>(
              tile_bytes, aifocore::Size<uint32_t, 2>{tile_w, tile_h},
              tile_channels);
          cache->Put(key, std::move(entry));
        }

        return readers::simpletiff_exec::PaintTileMaybeLocked(
            writer, paint_op,
            std::span<const uint8_t>(tile_bytes.data(), tile_bytes.size()),
            tile_w, tile_h, tile_channels, writer_mutex);
      });
}

}  // namespace fastslide::formats::olympusvsi
