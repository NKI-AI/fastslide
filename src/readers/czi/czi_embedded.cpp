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

/// @file czi_embedded.cpp
/// @brief Implementation of embedded-CZI parsing/decoding helpers.

#include "fastslide/readers/czi/czi_embedded.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zstd.h>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/runtime/io/ascii_utils.h"
#include "fastslide/runtime/io/binary_utils.h"

namespace fastslide {
namespace czi {
namespace internal {

namespace {

using runtime::io::ReadFixedAscii;
using runtime::io::ReadLeInt32;
using runtime::io::ReadLeInt64;
using runtime::io::StartsWithMagic;

constexpr std::string_view kSidZisRawFile = "ZISRAWFILE";
constexpr std::string_view kSidZisRawDirectory = "ZISRAWDIRECTORY";
constexpr std::string_view kSidZisRawSubblock = "ZISRAWSUBBLOCK";
constexpr std::string_view kSchemaDv = "DV";

/// @brief Choose a 16-bit denominator for normalising BGR48 to 8-bit RGB.
///
/// Picks `(2^bits)-1` (or `2^(bits-1)-1` if `max_val` is itself a power of
/// two), clamped to at least 255. This avoids dark output for instruments
/// that store ~12-bit samples in 16-bit channels.
uint16_t ChooseU16Denom(uint16_t max_val) {
  if (max_val == 0) {
    return 255;
  }
  unsigned bits = 0;
  uint16_t tmp = max_val;
  while (tmp != 0) {
    ++bits;
    tmp >>= 1;
  }
  const auto pow2 = static_cast<uint16_t>(1u << (bits - 1));
  uint16_t denom = 0;
  if (max_val == pow2) {
    denom = static_cast<uint16_t>(pow2 - 1);
  } else {
    denom = static_cast<uint16_t>((1u << bits) - 1u);
  }
  return std::max<uint16_t>(denom, 255);
}

uint8_t ScaleU16ToU8(uint16_t v, uint16_t denom) {
  const uint32_t out =
      (static_cast<uint32_t>(v) * 255u + static_cast<uint32_t>(denom) / 2u) /
      static_cast<uint32_t>(denom);
  return static_cast<uint8_t>(std::min<uint32_t>(255u, out));
}

aifocore::Result<std::vector<uint8_t>> DecompressZstd(
    std::span<const uint8_t> in, size_t expected_size) {
  std::vector<uint8_t> out(expected_size);
  const size_t res =
      ZSTD_decompress(out.data(), out.size(), in.data(), in.size());
  if (ZSTD_isError(res)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("ZSTD_decompress failed: {}",
                              ZSTD_getErrorName(res)));
  }
  if (res != expected_size) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("ZSTD size mismatch: got {}, expected {}", res,
                              expected_size));
  }
  return out;
}

aifocore::Result<std::vector<uint8_t>> UnpackHiLo16(
    std::span<const uint8_t> in) {
  if ((in.size() % 2) != 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
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
  // zstd1 payload starts with a tiny header. The first byte is the header
  // length. Known values:
  // - 1: header is just the length byte, no options.
  // - 3: includes (chunk_type, is_hi_low_pack).
  if (in.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "zstd1 payload truncated");
  }
  const uint8_t header_len = in[0];
  if (header_len == 1) {
    return Zstd1ParseResult{.payload = in.subspan(1), .do_hilo = false};
  }
  if (header_len == 3) {
    if (in.size() < 3) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "zstd1 payload truncated (header)");
    }
    const uint8_t chunk_type = in[1];
    const uint8_t flags = in[2];
    if (chunk_type != 1) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "zstd1 payload has unsupported chunk type");
    }
    return Zstd1ParseResult{.payload = in.subspan(3),
                            .do_hilo = (flags & 1u) != 0u};
  }
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                              "zstd1 payload has unsupported header length");
}

}  // namespace

aifocore::Result<EmbeddedSubblock> ParseEmbeddedSingleSubblock(
    FileReader& file, int64_t base_offset) {
  // Read embedded file header (same layout as normal CZI, but starting at
  // base_offset).
  AIFOCORE_RETURN_IF_ERROR(file.Seek(base_offset));

  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawFile)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: bad file magic");
  }

  (void)ReadLeInt64(file.Get());  // allocated_size
  (void)ReadLeInt64(file.Get());  // used_size
  (void)ReadLeInt32(file.Get());  // major
  (void)ReadLeInt32(file.Get());  // minor
  (void)ReadLeInt32(file.Get());  // reserved1
  (void)ReadLeInt32(file.Get());  // reserved2
  char guid1[16];
  char guid2[16];
  AIFOCORE_RETURN_IF_ERROR(file.Read(guid1, sizeof(guid1)));
  AIFOCORE_RETURN_IF_ERROR(file.Read(guid2, sizeof(guid2)));
  (void)ReadLeInt32(file.Get());  // file_part

  int64_t subblk_dir_pos = 0;
  AIFOCORE_ASSIGN_OR_RETURN(subblk_dir_pos, ReadLeInt64(file.Get()));
  (void)ReadLeInt64(file.Get());  // meta_pos
  (void)ReadLeInt32(file.Get());  // update_pending
  (void)ReadLeInt64(file.Get());  // att_dir_pos

  if (subblk_dir_pos == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: missing subblock directory");
  }

  // Read subblock directory segment header (embedded CZI):
  // - sid[16] ("ZISRAWDIRECTORY")
  // - allocated_size (int64), used_size (int64)
  // - entry_count (int32)
  // - reserved[124]
  AIFOCORE_RETURN_IF_ERROR(file.Seek(base_offset + subblk_dir_pos));
  std::memset(sid_raw, 0, sizeof(sid_raw));
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string dir_sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(dir_sid, kSidZisRawDirectory)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: bad directory magic");
  }

  int64_t allocated_size = 0;
  int64_t used_size = 0;
  int32_t entry_count = 0;
  AIFOCORE_ASSIGN_OR_RETURN(allocated_size, ReadLeInt64(file.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(used_size, ReadLeInt64(file.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(entry_count, ReadLeInt32(file.Get()));
  (void)allocated_size;
  char reserved_dir[124];
  AIFOCORE_RETURN_IF_ERROR(file.Read(reserved_dir, sizeof(reserved_dir)));

  if (entry_count != 1) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Embedded CZI: expected 1 subblock, got {}",
                              entry_count));
  }

  // Read single DV entry and its dimensions from directory segment buffer.
  const int64_t header_size = 16 + 8 + 8 + 4 + 124;
  const int64_t seg_hdr_size = 16 + 8 + 8;
  const int64_t seg_size = used_size - header_size + seg_hdr_size;
  if (seg_size <= 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: invalid directory seg_size");
  }

  AIFOCORE_ASSIGN_OR_RETURN(auto buf,
                            file.ReadBytes(static_cast<size_t>(seg_size)));
  size_t p = 0;
  if (buf.size() < 2) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: directory truncated");
  }
  const std::string schema(reinterpret_cast<const char*>(buf.data()), 2);
  p += 2;
  if (schema != kSchemaDv) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: unexpected schema");
  }
  if (p + 4 + 8 + 4 + 4 + 1 + 1 + 4 + 4 > buf.size()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: directory entry truncated");
  }

  int32_t pixel_type = 0;
  std::memcpy(&pixel_type, buf.data() + p, sizeof(pixel_type));
  p += 4;
  int64_t file_pos = 0;
  std::memcpy(&file_pos, buf.data() + p, sizeof(file_pos));
  p += 8;
  // file_part
  p += 4;
  int32_t compression = 0;
  std::memcpy(&compression, buf.data() + p, sizeof(compression));
  p += 4;
  // pyramid_type + reserved
  p += 1;
  p += 1;
  p += 4;
  int32_t ndimensions = 0;
  std::memcpy(&ndimensions, buf.data() + p, sizeof(ndimensions));
  p += 4;

  int32_t x = 0;
  int32_t y = 0;
  uint32_t w = 0;
  uint32_t h = 0;

  for (int d = 0; d < ndimensions; ++d) {
    if (p + 4 + 4 + 4 + 4 + 4 > buf.size()) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Embedded CZI: dimension entry truncated");
    }
    char dim_raw[4];
    std::memcpy(dim_raw, buf.data() + p, sizeof(dim_raw));
    p += 4;
    const std::string dim_name = ReadFixedAscii(dim_raw, sizeof(dim_raw));
    int32_t start = 0;
    int32_t size0 = 0;
    float start_coord = 0.0f;
    int32_t stored_size = 0;
    std::memcpy(&start, buf.data() + p, sizeof(start));
    p += 4;
    std::memcpy(&size0, buf.data() + p, sizeof(size0));
    p += 4;
    std::memcpy(&start_coord, buf.data() + p, sizeof(start_coord));
    p += 4;
    std::memcpy(&stored_size, buf.data() + p, sizeof(stored_size));
    p += 4;
    (void)size0;
    (void)start_coord;
    if (dim_name == "X") {
      x = start;
      w = static_cast<uint32_t>(std::max(0, stored_size));
    } else if (dim_name == "Y") {
      y = start;
      h = static_cast<uint32_t>(std::max(0, stored_size));
    }
  }

  if (w == 0 || h == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: missing X/Y dims");
  }

  EmbeddedSubblock sb{};
  sb.file_pos = file_pos;
  sb.pixel_type = pixel_type;
  sb.compression = compression;
  sb.x = x;
  sb.y = y;
  sb.w = w;
  sb.h = h;
  return sb;
}

aifocore::Result<std::vector<uint8_t>> ReadEmbeddedSubblockRgb8(
    FileReader& file, int64_t base_offset, const EmbeddedSubblock& sb) {
  // Subblock segment header (embedded CZI, little-endian):
  // - sid[16] ("ZISRAWSUBBLOCK")
  // - allocated_size (int64), used_size (int64)
  // - meta_size (int32), attach_size (int32), data_size (int64)
  AIFOCORE_RETURN_IF_ERROR(file.Seek(base_offset + sb.file_pos));
  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawSubblock)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: bad subblock magic");
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

  const int64_t data_pos =
      base_offset + sb.file_pos + 288 + static_cast<int64_t>(meta_size);
  AIFOCORE_RETURN_IF_ERROR(file.Seek(data_pos));
  AIFOCORE_ASSIGN_OR_RETURN(auto payload,
                            file.ReadBytes(static_cast<size_t>(data_size)));

  const size_t expected = static_cast<size_t>(sb.w) *
                          static_cast<size_t>(sb.h) *
                          ((sb.pixel_type == 4) ? 6 : 3);

  std::vector<uint8_t> raw = std::move(payload);

  if (sb.compression == 5 || sb.compression == 6) {
    bool do_hilo = false;
    std::span<const uint8_t> zpayload(raw);
    if (sb.compression == 6) {
      AIFOCORE_ASSIGN_OR_RETURN(const auto parsed, ParseZstd1Payload(zpayload));
      zpayload = parsed.payload;
      do_hilo = parsed.do_hilo;
    }

    AIFOCORE_ASSIGN_OR_RETURN(raw, DecompressZstd(zpayload, expected));
    if (do_hilo) {
      AIFOCORE_ASSIGN_OR_RETURN(raw, UnpackHiLo16(raw));
    }
  } else if (sb.compression != 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                                "Embedded CZI: unsupported compression");
  }

  // Convert BGR24/BGR48 -> RGB8. For BGR48, use max-based scaling to avoid
  // dark output.
  const size_t npx = static_cast<size_t>(sb.w) * sb.h;
  if (sb.pixel_type == 3) {
    if (raw.size() != npx * 3) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Embedded CZI: BGR24 size mismatch");
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
  if (sb.pixel_type == 4) {
    if (raw.size() != npx * 6) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Embedded CZI: BGR48 size mismatch");
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
      rgb[i * 3 + 0] = ScaleU16ToU8(r16, denom);
      rgb[i * 3 + 1] = ScaleU16ToU8(g16, denom);
      rgb[i * 3 + 2] = ScaleU16ToU8(b16, denom);
    }
    return rgb;
  }

  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                              "Embedded CZI: unsupported pixel type");
}

}  // namespace internal
}  // namespace czi
}  // namespace fastslide
