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

#include "fastslide/readers/czi/czi_tile_executor.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <csetjmp>
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

#include <zstd.h>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/readers/czi/czi.h"
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
constexpr size_t kCziSubblockHdrLen = 288;

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

uint32_t ScaleU16ToU8(uint16_t v, uint16_t denom) {
  return static_cast<uint32_t>(
      (static_cast<uint32_t>(v) * 255u + static_cast<uint32_t>(denom) / 2u) /
      static_cast<uint32_t>(denom));
}

uint16_t ChooseU16Denom(uint16_t max_val) {
  if (max_val == 0) {
    return 255;
  }
  const unsigned bits = std::bit_width<uint16_t>(max_val);
  const uint16_t pow2 = static_cast<uint16_t>(1u << (bits - 1));
  uint16_t denom = 0;
  if (max_val == pow2) {
    denom = static_cast<uint16_t>(pow2 - 1);
  } else {
    denom = static_cast<uint16_t>((1u << bits) - 1u);
  }
  return std::max<uint16_t>(denom, 255);
}

aifocore::Result<std::vector<uint8_t>> UnpackHiLo16(
    std::span<const uint8_t> in) {
  if ((in.size() % 2) != 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "HiLo unpacking requires even byte count");
  }
  const size_t half = in.size() / 2;
  std::vector<uint8_t> out(in.size());
  for (size_t i = 0; i < half; ++i) {
    out[i * 2] = in[i];
    out[i * 2 + 1] = in[half + i];
  }
  return out;
}

struct Zstd1ParseResult {
  std::span<const uint8_t> payload;
  bool do_hilo = false;
};

aifocore::Result<Zstd1ParseResult> ParseZstd1Payload(
    std::span<const uint8_t> in) {
  // ZSTD1 payloads start with a small container header:
  // - byte[0] == header_size_in_bytes (including byte[0])
  // - if header_size == 3: byte[1] is chunk type (expected 1), byte[2] are
  // flags
  if (in.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "zstd1 payload truncated");
  }

  const uint8_t header_len = in[0];
  if (header_len == 1) {
    return Zstd1ParseResult{.payload = in.subspan(1), .do_hilo = false};
  }

  if (header_len == 3) {
    if (in.size() < 3) {
      return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                              "zstd1 payload truncated (header)");
    }
    const uint8_t chunk_type = in[1];
    const uint8_t flags = in[2];
    if (chunk_type != 1) {
      return aifocore::Status(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Unexpected zstd1 chunk type: {}", chunk_type));
    }
    return Zstd1ParseResult{.payload = in.subspan(3),
                            .do_hilo = (flags & 1u) != 0u};
  }

  return aifocore::Status(
      aifocore::StatusCode::kInvalidArgument,
      aifocore::fmt::format("Unexpected zstd1 header length: {}", header_len));
}

aifocore::Result<std::vector<uint8_t>> DecompressZstd(
    std::span<const uint8_t> in, size_t expected_size) {
  std::vector<uint8_t> out(expected_size);
  const size_t res =
      ZSTD_decompress(out.data(), out.size(), in.data(), in.size());
  if (ZSTD_isError(res)) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            aifocore::fmt::format("ZSTD_decompress failed: {}",
                                                  ZSTD_getErrorName(res)));
  }
  if (res != expected_size) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("ZSTD size mismatch: got {}, expected {}", res,
                              expected_size));
  }
  return out;
}

aifocore::Result<std::vector<uint8_t>> ConvertRawToRgb8(
    PixelType pixel_type, uint32_t w, uint32_t h,
    std::span<const uint8_t> raw) {
  const size_t npx = static_cast<size_t>(w) * h;

  switch (pixel_type) {
    case PixelType::kGray8: {
      if (raw.size() != npx) {
        return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
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
        return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                                "GRAY16 size mismatch");
      }
      const uint16_t* u16 = reinterpret_cast<const uint16_t*>(raw.data());
      uint16_t max_v = 0;
      for (size_t i = 0; i < npx; ++i) {
        max_v = std::max(max_v, u16[i]);
      }
      const uint16_t denom = ChooseU16Denom(max_v);
      std::vector<uint8_t> rgb(npx * 3);
      for (size_t i = 0; i < npx; ++i) {
        const uint8_t g = static_cast<uint8_t>(ScaleU16ToU8(u16[i], denom));
        rgb[i * 3 + 0] = g;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = g;
      }
      return rgb;
    }
    case PixelType::kBgr24: {
      if (raw.size() != npx * 3) {
        return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
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
        return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                                "BGR48 size mismatch");
      }
      const uint16_t* u16 = reinterpret_cast<const uint16_t*>(raw.data());
      uint16_t max_v = 0;
      for (size_t i = 0; i < npx * 3; ++i) {
        max_v = std::max(max_v, u16[i]);
      }
      const uint16_t denom = ChooseU16Denom(max_v);
      std::vector<uint8_t> rgb(npx * 3);
      for (size_t i = 0; i < npx; ++i) {
        const uint16_t b16 = u16[i * 3 + 0];
        const uint16_t g16 = u16[i * 3 + 1];
        const uint16_t r16 = u16[i * 3 + 2];
        rgb[i * 3 + 0] = static_cast<uint8_t>(ScaleU16ToU8(r16, denom));
        rgb[i * 3 + 1] = static_cast<uint8_t>(ScaleU16ToU8(g16, denom));
        rgb[i * 3 + 2] = static_cast<uint8_t>(ScaleU16ToU8(b16, denom));
      }
      return rgb;
    }
  }

  return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                          "Unsupported pixel type");
}

}  // namespace

aifocore::Status CziTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                              const CziReader& reader,
                                              runtime::TileWriter& writer) {
  if (plan.operations.empty()) {
    const auto& bg = plan.output.background;
    return writer.FillBackground(bg.r, bg.g, bg.b);
  }

  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex writer_mutex;
  std::atomic<int> error_count{0};

  auto futures = pool.submit_sequence(0, plan.operations.size(), [&](size_t i) {
    const auto& op = plan.operations[i];
    auto st = ExecuteTileOperation(op, reader, writer, writer_mutex);
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
                                               const CziReader& reader) {
  return runtime::TileKey(reader.GetFilename(), static_cast<uint16_t>(op.level),
                          op.tile_coord.x, op.tile_coord.y);
}

aifocore::Result<DecodedTileData> CziTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const CziReader& reader) {
  const uint32_t subblock_index = op.tile_coord.x;
  const auto sb = reader.GetSubblockInfo(subblock_index);

  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(reader.GetFilename(), "rb"));
  AIFOCORE_RETURN_IF_ERROR(file.Seek(sb.file_pos));

  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawSubblock)) {
    return aifocore::Status(
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
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid subblock header sizes");
  }

  const int64_t data_pos =
      sb.file_pos + static_cast<int64_t>(kCziSubblockHdrLen) + meta_size;
  AIFOCORE_RETURN_IF_ERROR(file.Seek(data_pos));
  AIFOCORE_ASSIGN_OR_RETURN(auto payload,
                            file.ReadBytes(static_cast<size_t>(data_size)));

  const PixelType pt = static_cast<PixelType>(sb.pixel_type);
  const Compression comp = static_cast<Compression>(sb.compression);

  const size_t bpp = BytesPerPixel(pt);
  if (bpp == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Unsupported CZI pixel type");
  }
  const size_t expected_raw =
      static_cast<size_t>(sb.w) * static_cast<size_t>(sb.h) * bpp;

  std::vector<uint8_t> raw;

  if (comp == Compression::kNone) {
    raw = std::move(payload);
    if (raw.size() != expected_raw) {
      return aifocore::Status(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("COMP_NONE size mismatch: got {}, expected {}",
                                raw.size(), expected_raw));
    }
  } else if (comp == Compression::kZstd0 || comp == Compression::kZstd1) {
    bool do_hilo = false;
    std::span<const uint8_t> zpayload(payload);
    if (comp == Compression::kZstd1) {
      AIFOCORE_ASSIGN_OR_RETURN(const auto parsed, ParseZstd1Payload(zpayload));
      zpayload = parsed.payload;
      do_hilo = parsed.do_hilo;
    }

    AIFOCORE_ASSIGN_OR_RETURN(raw, DecompressZstd(zpayload, expected_raw));
    if (do_hilo) {
      AIFOCORE_ASSIGN_OR_RETURN(auto unpacked, UnpackHiLo16(raw));
      raw = std::move(unpacked);
    }
  } else if (comp == Compression::kJxr) {
    const auto expected = runtime::decoders::ExpectedDimensions{sb.w, sb.h};
    AIFOCORE_ASSIGN_OR_RETURN(
        auto decoded, runtime::decoders::DecodeJpegXrToRgb(payload, expected));
    auto& tile_buf = GetBuffers().tile_buffer;
    tile_buf = std::move(decoded.rgb);
    return DecodedTileData{
        std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w, sb.h,
        3};
  } else if (comp == Compression::kJpeg) {
    AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                              runtime::decoders::DecodeJpegToRgb(payload));
    auto& tile_buf = GetBuffers().tile_buffer;
    tile_buf = std::move(decoded.rgb);
    return DecodedTileData{
        std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w, sb.h,
        3};
  } else {
    return aifocore::Status(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("Unsupported CZI compression: {}",
                              sb.compression));
  }

  // Convert raw pixel bytes to RGB8.
  AIFOCORE_ASSIGN_OR_RETURN(auto rgb, ConvertRawToRgb8(pt, sb.w, sb.h, raw));
  auto& tile_buf = GetBuffers().tile_buffer;
  tile_buf = std::move(rgb);
  return DecodedTileData{
      std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), sb.w, sb.h,
      3};
}

aifocore::Status CziTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const CziReader& reader,
    runtime::TileWriter& writer, std::mutex& writer_mutex) {
  auto tile_data_or = ReadWithCache(op, reader);
  if (!tile_data_or.ok()) {
    return aifocore::Status::OkStatus();
  }

  const uint32_t subblock_index = op.tile_coord.x;
  const auto sb = reader.GetSubblockInfo(subblock_index);
  const uint32_t tile_w = sb.w;
  const uint32_t tile_h = sb.h;

  std::span<const uint8_t> data_to_write = *tile_data_or;
  uint32_t write_w = tile_w;
  uint32_t write_h = tile_h;
  core::TileReadOp write_op = op;

  const bool needs_crop = op.transform.source.x != 0 ||
                          op.transform.source.y != 0 ||
                          op.transform.source.width != tile_w ||
                          op.transform.source.height != tile_h;

  if (needs_crop) {
    const uint32_t crop_x = op.transform.source.x;
    const uint32_t crop_y = op.transform.source.y;
    const uint32_t want_w = op.transform.source.width;
    const uint32_t want_h = op.transform.source.height;

    if (crop_x >= tile_w || crop_y >= tile_h) {
      return aifocore::Status::OkStatus();
    }
    write_w = std::min(want_w, tile_w - crop_x);
    write_h = std::min(want_h, tile_h - crop_y);
    if (write_w == 0 || write_h == 0) {
      return aifocore::Status::OkStatus();
    }

    const size_t row_bytes = static_cast<size_t>(write_w) * 3;
    const size_t out_bytes = row_bytes * write_h;
    uint8_t* dst = GetBuffers().GetCropBuffer(out_bytes);

    const size_t src_stride = static_cast<size_t>(tile_w) * 3;
    for (uint32_t y = 0; y < write_h; ++y) {
      const size_t src_off = (static_cast<size_t>(crop_y + y) * src_stride) +
                             static_cast<size_t>(crop_x) * 3;
      std::memcpy(dst + static_cast<size_t>(y) * row_bytes,
                  data_to_write.data() + src_off, row_bytes);
    }

    data_to_write = std::span<const uint8_t>(dst, out_bytes);
    write_op.transform.source.x = 0;
    write_op.transform.source.y = 0;
    write_op.transform.source.width = write_w;
    write_op.transform.source.height = write_h;
    write_op.transform.dest.width =
        std::min(write_op.transform.dest.width, write_w);
    write_op.transform.dest.height =
        std::min(write_op.transform.dest.height, write_h);
  }

  auto st = writer.WriteTile(write_op, data_to_write, write_w, write_h, 3,
                             writer_mutex);
  if (!st.ok()) {
    return aifocore::Status::OkStatus();
  }
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide
