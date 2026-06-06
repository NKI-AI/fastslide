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

/// @file czi_tile_executor.cpp
/// @brief CZI tile decode + paint stage.
///
/// Based on the BSD-3-Clause `czifile` library (Christoph Gohlke).

#include "fastslide/readers/czi/czi_tile_executor.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/readers/czi/czi_parse.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/runtime/decoders/jpeg_xr_decoder.h"
#include "fastslide/runtime/io/ascii_utils.h"
#include "fastslide/runtime/io/binary_utils.h"
#include "fastslide/runtime/io/file_reader.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

namespace {

using runtime::io::ReadFixedAscii;
using runtime::io::StartsWithMagic;

constexpr std::string_view kSidZisRawSubblock = "ZISRAWSUBBLOCK";

enum class Compression : int32_t {
  kNone = 0,
  kJpeg = 1,
  kLzw = 2,
  kJxr = 4,
  kZstd0 = 5,
  kZstd1 = 6,
};

enum class PixelType : int32_t {
  kGray8 = 0,
  kGray16 = 1,
  kBgr24 = 3,
  kBgr48 = 4,
};

size_t BytesPerPixel(PixelType pt) {
  switch (pt) {
    case PixelType::kGray8:
      return 1;
    case PixelType::kGray16:
      return 2;
    case PixelType::kBgr24:
      return 3;
    case PixelType::kBgr48:
      return 6;
  }
  return 0;
}

/// @brief Convert a decoded subblock buffer to packed RGB, preserving depth.
///
/// 8-bit pixel types yield 8-bit RGB; 16-bit pixel types yield native 16-bit
/// RGB with no rescaling (matching `czifile`, which keeps the original dtype).
/// Grayscale is broadcast across the three channels. Output bytes are
/// little-endian, ready for the Canvas RGB8/RGB16 paint paths.
aifocore::Result<std::vector<uint8_t>> ConvertRawToRgb(
    PixelType pixel_type, uint32_t w, uint32_t h,
    std::span<const uint8_t> raw) {
  const size_t npx = static_cast<size_t>(w) * h;

  switch (pixel_type) {
    case PixelType::kGray8: {
      if (raw.size() != npx) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "GRAY8 size mismatch");
      }
      std::vector<uint8_t> rgb(npx * 3);
      for (size_t i = 0; i < npx; ++i) {
        const uint8_t g = raw[i];
        rgb[i * 3 + 0] = g;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = g;
      }
      return rgb;
    }
    case PixelType::kGray16: {
      if (raw.size() != npx * 2) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "GRAY16 size mismatch");
      }
      const uint16_t* u16 = reinterpret_cast<const uint16_t*>(raw.data());
      std::vector<uint16_t> rgb(npx * 3);
      for (size_t i = 0; i < npx; ++i) {
        const uint16_t g = u16[i];
        rgb[i * 3 + 0] = g;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = g;
      }
      std::vector<uint8_t> out(rgb.size() * sizeof(uint16_t));
      std::memcpy(out.data(), rgb.data(), out.size());
      return out;
    }
    case PixelType::kBgr24: {
      if (raw.size() != npx * 3) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "BGR24 size mismatch");
      }
      std::vector<uint8_t> rgb(npx * 3);
      for (size_t i = 0; i < npx; ++i) {
        const uint8_t b = raw[i * 3 + 0];
        const uint8_t g = raw[i * 3 + 1];
        const uint8_t r = raw[i * 3 + 2];
        rgb[i * 3 + 0] = r;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = b;
      }
      return rgb;
    }
    case PixelType::kBgr48: {
      if (raw.size() != npx * 6) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "BGR48 size mismatch");
      }
      const uint16_t* u16 = reinterpret_cast<const uint16_t*>(raw.data());
      std::vector<uint16_t> rgb(npx * 3);
      for (size_t i = 0; i < npx; ++i) {
        rgb[i * 3 + 0] = u16[i * 3 + 2];  // R
        rgb[i * 3 + 1] = u16[i * 3 + 1];  // G
        rgb[i * 3 + 2] = u16[i * 3 + 0];  // B
      }
      std::vector<uint8_t> out(rgb.size() * sizeof(uint16_t));
      std::memcpy(out.data(), rgb.data(), out.size());
      return out;
    }
  }

  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                              "Unsupported pixel type");
}

/// @brief Extract a single grayscale plane from raw subblock bytes.
///
/// For multi-channel (spectral) CZI scenes each subblock is one channel plane
/// and must be painted into its own output channel rather than broadcast into
/// an RGB triplet. GRAY8/GRAY16 raw payloads already contain exactly one
/// sample per pixel, so the bytes pass through unchanged at their native
/// depth. BGR pixel types are not expected on spectral scenes.
aifocore::Result<std::vector<uint8_t>> ConvertRawToPlane(
    PixelType pixel_type, uint32_t w, uint32_t h,
    std::span<const uint8_t> raw) {
  const size_t npx = static_cast<size_t>(w) * h;
  switch (pixel_type) {
    case PixelType::kGray8:
      if (raw.size() != npx) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "GRAY8 plane size mismatch");
      }
      return std::vector<uint8_t>(raw.begin(), raw.end());
    case PixelType::kGray16:
      if (raw.size() != npx * 2) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "GRAY16 plane size mismatch");
      }
      return std::vector<uint8_t>(raw.begin(), raw.end());
    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          "Spectral CZI scenes only support grayscale pixel types");
  }
}

/// @brief Take channel 0 of an interleaved 8-bit RGB buffer (gray broadcast).
std::vector<uint8_t> ExtractPlane8(std::span<const uint8_t> rgb, size_t npx) {
  std::vector<uint8_t> plane(npx);
  for (size_t i = 0; i < npx; ++i) {
    plane[i] = rgb[i * 3];
  }
  return plane;
}

/// @brief Take channel 0 of an interleaved 16-bit RGB buffer (gray broadcast).
std::vector<uint8_t> ExtractPlane16(std::span<const uint16_t> rgb16,
                                    size_t npx) {
  std::vector<uint8_t> plane(npx * 2);
  uint16_t* out = reinterpret_cast<uint16_t*>(plane.data());
  for (size_t i = 0; i < npx; ++i) {
    out[i] = rgb16[i * 3];
  }
  return plane;
}

}  // namespace

aifocore::Status CziTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                              const CziExecContext& context,
                                              runtime::Canvas& writer) {
  if (plan.operations.empty()) {
    const auto& bg = plan.output.background;
    return writer.FillBackground(bg.r, bg.g, bg.b);
  }

  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex writer_mutex;
  std::atomic<int> error_count{0};

  auto futures = pool.submit_sequence(0, plan.operations.size(), [&](size_t i) {
    const auto& op = plan.operations[i];
    auto st = ExecuteTileOperation(op, context, writer, writer_mutex);
    if (!st.ok()) {
      const int n = ++error_count;
      if (n <= 10) {
        std::cerr << "CZI tile op failed: " << st.ToString() << "\n";
      }
    }
  });

  futures.wait();
  return aifocore::Status::OkStatus();
}

runtime::TileKey CziTileExecutor::MakeCacheKey(const core::TileReadOp& op,
                                               const CziExecContext& context) {
  // `source_id` is the (globally unique) subblock index; that alone keys the
  // tile uniquely across channels, focal planes and time points.
  return runtime::TileKey(std::string(context.GetFilename()),
                          static_cast<uint16_t>(op.level), op.source_id,
                          op.tile_coord.x);
}

aifocore::Result<DecodedTileData> CziTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const CziExecContext& context) {
  const uint32_t subblock_index = op.source_id;
  const bool spectral = context.IsSpectral();
  const auto& sb = context.GetSubblockInfo(subblock_index);

  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(
      file, FileReader::Open(std::string(context.GetFilename()), "rb"));
  AIFOCORE_RETURN_IF_ERROR(file.Seek(sb.file_pos));

  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawSubblock)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Bad subblock magic: {}", sid));
  }

  (void)ReadLeInt64(file.Get());  // allocated_size
  (void)ReadLeInt64(file.Get());  // used_size
  int32_t meta_size = 0;
  int32_t attach_size = 0;
  int64_t data_size = 0;
  AIFOCORE_ASSIGN_OR_RETURN(meta_size, ReadLeInt32(file.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(attach_size, ReadLeInt32(file.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(data_size, ReadLeInt64(file.Get()));
  (void)attach_size;
  if (meta_size < 0 || data_size < 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid subblock header sizes");
  }

  const int64_t data_pos =
      sb.file_pos +
      static_cast<int64_t>(czi::SubblockFixedHeaderLength(sb.dim_count)) +
      meta_size;
  AIFOCORE_RETURN_IF_ERROR(file.Seek(data_pos));
  AIFOCORE_ASSIGN_OR_RETURN(auto payload,
                            file.ReadBytes(static_cast<size_t>(data_size)));

  const PixelType pt = static_cast<PixelType>(sb.pixel_type);
  const Compression comp = static_cast<Compression>(sb.compression);

  const size_t bpp = BytesPerPixel(pt);
  if (bpp == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Unsupported CZI pixel type");
  }
  const size_t expected_raw =
      static_cast<size_t>(sb.w) * static_cast<size_t>(sb.h) * bpp;

  std::vector<uint8_t> raw;

  if (comp == Compression::kNone) {
    raw = std::move(payload);
    if (raw.size() != expected_raw) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("COMP_NONE size mismatch: got {}, expected {}",
                                raw.size(), expected_raw));
    }
  } else if (comp == Compression::kZstd0 || comp == Compression::kZstd1) {
    bool do_hilo = false;
    std::span<const uint8_t> zpayload(payload);
    if (comp == Compression::kZstd1) {
      AIFOCORE_ASSIGN_OR_RETURN(const auto parsed,
                                czi::ParseZstd1Payload(zpayload));
      zpayload = parsed.payload;
      do_hilo = parsed.do_hilo;
    }

    AIFOCORE_ASSIGN_OR_RETURN(raw, czi::DecompressZstd(zpayload, expected_raw));
    if (do_hilo) {
      AIFOCORE_ASSIGN_OR_RETURN(auto unpacked, czi::UnpackHiLo16(raw));
      raw = std::move(unpacked);
    }
  } else if (comp == Compression::kJxr) {
    const auto expected = runtime::decoders::ExpectedDimensions{sb.w, sb.h};
    const size_t npx = static_cast<size_t>(sb.w) * sb.h;
    auto& tile_buf = GetBuffers().tile_buffer;
    if (spectral) {
      // One channel plane per subblock, at native bit depth. The JPEG-XR
      // decoders broadcast a grayscale source across the three RGB planes, so
      // channel 0 carries the value we want.
      if (pt == PixelType::kGray16) {
        AIFOCORE_ASSIGN_OR_RETURN(
            auto decoded,
            runtime::decoders::DecodeJpegXrToRgb16(payload, expected));
        tile_buf = ExtractPlane16(decoded.rgb, npx);
      } else {
        AIFOCORE_ASSIGN_OR_RETURN(
            auto decoded,
            runtime::decoders::DecodeJpegXrToRgb(payload, expected));
        tile_buf = ExtractPlane8(decoded.rgb, npx);
      }
      return DecodedTileData{
          std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w,
          sb.h, 1};
    }
    AIFOCORE_ASSIGN_OR_RETURN(
        auto decoded, runtime::decoders::DecodeJpegXrToRgb(payload, expected));
    tile_buf = std::move(decoded.rgb);
    return DecodedTileData{
        std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w, sb.h,
        3};
  } else if (comp == Compression::kJpeg) {
    AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                              runtime::decoders::DecodeJpegToRgb(payload));
    auto& tile_buf = GetBuffers().tile_buffer;
    if (spectral) {
      const size_t npx = static_cast<size_t>(sb.w) * sb.h;
      tile_buf = ExtractPlane8(decoded.rgb, npx);
      return DecodedTileData{
          std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w,
          sb.h, 1};
    }
    tile_buf = std::move(decoded.rgb);
    return DecodedTileData{
        std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w, sb.h,
        3};
  } else {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("Unsupported CZI compression: {}",
                              sb.compression));
  }

  auto& tile_buf = GetBuffers().tile_buffer;
  if (spectral) {
    // Spectral scenes paint one channel plane per subblock at native depth.
    AIFOCORE_ASSIGN_OR_RETURN(auto plane,
                              ConvertRawToPlane(pt, sb.w, sb.h, raw));
    tile_buf = std::move(plane);
    return DecodedTileData{
        std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w, sb.h,
        1};
  }

  // Convert raw pixel bytes to packed RGB, preserving the native bit depth
  // (8-bit types stay 8-bit, 16-bit types stay 16-bit; see ConvertRawToRgb).
  AIFOCORE_ASSIGN_OR_RETURN(auto rgb, ConvertRawToRgb(pt, sb.w, sb.h, raw));
  tile_buf = std::move(rgb);
  return DecodedTileData{
      std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w, sb.h,
      3};
}

aifocore::Status CziTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const CziExecContext& context,
    runtime::Canvas& writer, std::mutex& writer_mutex) {
  auto tile_data_or = ReadWithCache(op, context);
  if (!tile_data_or.ok()) {
    return aifocore::Status::OkStatus();
  }

  const uint32_t subblock_index = op.source_id;
  const auto& sb = context.GetSubblockInfo(subblock_index);
  const uint32_t tile_w = sb.w;
  const uint32_t tile_h = sb.h;
  // Spectral scenes decode one channel plane per subblock; RGB scenes decode
  // a 3-channel triplet.
  const uint32_t tile_channels = context.IsSpectral() ? 1u : 3u;

  const std::span<const uint8_t> tile_data = *tile_data_or;
  // Canvas extracts sub-regions from full tiles when op.transform.source
  // specifies one.
  auto st = writer.PaintTile(op, tile_data, tile_w, tile_h, tile_channels,
                             writer_mutex);
  if (!st.ok()) {
    return aifocore::Status::OkStatus();
  }
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide
