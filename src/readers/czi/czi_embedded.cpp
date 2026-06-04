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
///
/// Clean-room implementation from the public Zeiss CZI / ZISRAW binary-format
/// definition, using the permissively licensed `czifile` Python library
/// (Christoph Gohlke, BSD-3-Clause) as the on-disk-layout reference. The shared
/// ZISRAW decoders live in `czi_parse`.

#include "fastslide/readers/czi/czi_embedded.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/czi/czi_parse.h"
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

  int32_t entry_count = 0;
  (void)ReadLeInt64(file.Get());  // allocated_size
  (void)ReadLeInt64(file.Get());  // used_size
  AIFOCORE_ASSIGN_OR_RETURN(entry_count, ReadLeInt32(file.Get()));
  char reserved_dir[124];
  AIFOCORE_RETURN_IF_ERROR(file.Read(reserved_dir, sizeof(reserved_dir)));

  if (entry_count != 1) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Embedded CZI: expected 1 subblock, got {}",
                              entry_count));
  }

  // Read the single DirectoryEntryDV straight from the stream using the same
  // pure decoders as the top-level directory: a 32-byte fixed header followed
  // by `dimension_count` 20-byte dimension records. No derived segment-size
  // arithmetic is required.
  AIFOCORE_ASSIGN_OR_RETURN(auto fixed, file.ReadBytes(kDirEntryFixedSize));
  AIFOCORE_ASSIGN_OR_RETURN(const auto header, ParseDirEntryHeader(fixed));

  int32_t x = 0;
  int32_t y = 0;
  uint32_t w = 0;
  uint32_t h = 0;
  if (header.dimension_count > 0) {
    const size_t dims_bytes =
        static_cast<size_t>(header.dimension_count) * kDimensionEntrySize;
    AIFOCORE_ASSIGN_OR_RETURN(auto dims_buf, file.ReadBytes(dims_bytes));
    std::span<const uint8_t> dims_span(dims_buf);
    for (int32_t d = 0; d < header.dimension_count; ++d) {
      AIFOCORE_ASSIGN_OR_RETURN(
          const auto dim, ParseDimensionRecord(dims_span.subspan(
                              static_cast<size_t>(d) * kDimensionEntrySize)));
      if (dim.axis == 'X') {
        x = dim.start;
        w = static_cast<uint32_t>(std::max(0, dim.stored_size));
      } else if (dim.axis == 'Y') {
        y = dim.start;
        h = static_cast<uint32_t>(std::max(0, dim.stored_size));
      }
    }
  }

  if (w == 0 || h == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Embedded CZI: missing X/Y dims");
  }

  EmbeddedSubblock sb{};
  sb.file_pos = header.file_position;
  sb.pixel_type = header.pixel_type;
  sb.compression = header.compression;
  sb.x = x;
  sb.y = y;
  sb.w = w;
  sb.h = h;
  sb.dim_count = header.dimension_count;
  return sb;
}

aifocore::Result<EmbeddedRgb> ReadEmbeddedSubblockRgb(
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
      base_offset + sb.file_pos +
      static_cast<int64_t>(SubblockFixedHeaderLength(sb.dim_count)) +
      static_cast<int64_t>(meta_size);
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

  // Associated images (label, slide preview, thumbnail) keep their native bit
  // depth: BGR24 -> 8-bit RGB, BGR48 -> native 16-bit RGB with no rescaling.
  // Channels are reordered BGR -> RGB exactly as czifile does (it reverses the
  // last axis and preserves the original dtype, `image[..., ::-1]`)
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
    return EmbeddedRgb{std::move(rgb), DataType::kUInt8};
  }
  if (sb.pixel_type == 4) {
    if (raw.size() != npx * 6) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Embedded CZI: BGR48 size mismatch");
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
    return EmbeddedRgb{std::move(out), DataType::kUInt16};
  }

  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                              "Embedded CZI: unsupported pixel type");
}

}  // namespace internal
}  // namespace czi
}  // namespace fastslide
