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

#include "fastslide/readers/olympusvsi/olympusvsi_ets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/runtime/io/file_reader.h"

namespace fs = std::filesystem;

namespace fastslide::formats::olympusvsi {

namespace {

constexpr std::array<uint8_t, 4> kSisMagic = {'S', 'I', 'S', 0};
constexpr std::array<uint8_t, 4> kEtsMagic = {'E', 'T', 'S', 0};

// SIS container header: 4-byte magic + 13 little-endian fields, total 64 B.
constexpr size_t kSisHeaderSize = 64;
// ETS image header prefix: 4-byte magic + 9 uint32 fields, total 40 B.
constexpr size_t kEtsPrefixSize = 40;
// 17 reserved/zero uint32 words between the ETS prefix and the per-channel
// background block.
constexpr size_t kEtsBackgroundPad = 17 * sizeof(uint32_t);
// Tile-record layout depends on the SIS-declared dimensionality
// ``ndim``. Each record packs:
//   * const_marker (u32, equals ``ndim``)
//   * ``ndim`` coordinate slots (u32 each): slot 0 is ``x``, slot 1 is
//     ``y``, the final slot is the pyramid ``level``, and the slots in
//     between carry the raw non-spatial axis indices (channel / Z / T).
//     The parser stores these middle slots verbatim; which slot means
//     which axis is resolved later from the `.vsi` dimension metadata
//     (see ``DimensionLayout`` in olympusvsi.cpp), never inferred here.
//   * (u64 offset, u32 n_bytes, u32 pad) trailer = 16 B.
// → record_size = 4 + 4*ndim + 16
//   pair_offset = 4 + 4*ndim
//   level_offset = 4 + 4*(ndim - 1)    (the last dim slot)
//
// Observed in the wild:
//   * Brightfield RGB (8-bit) writes ``ndim = 4`` → 36-byte records,
//     dims = (x, y, axis, level).
//   * Fluorescence (16-bit) writes ``ndim = 5`` → 40-byte records,
//     dims = (x, y, axis, axis, level).
constexpr size_t kTileRecordTrailerSize = 16;
constexpr size_t kTileRecordMinNdim = 4;
constexpr size_t kTileRecordMaxNdim = 6;

constexpr size_t TileRecordSize(uint32_t ndim) {
  return 4 + 4 * static_cast<size_t>(ndim) + kTileRecordTrailerSize;
}

constexpr size_t TileRecordPairOffset(uint32_t ndim) {
  return 4 + 4 * static_cast<size_t>(ndim);
}

constexpr size_t TileRecordLevelOffset(uint32_t ndim) {
  return 4 + 4 * static_cast<size_t>(ndim - 1);
}

// Header window: SIS + ETS prefix + background fit in ~176 B; 8 KiB covers
// larger background blocks in one read.
constexpr size_t kHeaderWindowBytes = 8192;

constexpr size_t kCodecProbeBytes = 16;

constexpr std::string_view kIssuesUrl =
    "https://github.com/NKI-AI/fastslide/issues";

struct CodecMagic {
  std::span<const uint8_t> magic;
  TileCodec codec;
};

constexpr uint8_t kJp2Codestream[] = {0xFF, 0x4F, 0xFF, 0x51};
constexpr uint8_t kJp2Box[] = {0x00, 0x00, 0x00, 0x0C, 'j', 'P', ' ', ' '};
// JPEG Start-Of-Image followed by the first marker prefix (0xFFD8FF...).
constexpr uint8_t kJpegSoi[] = {0xFF, 0xD8, 0xFF};

const CodecMagic kCodecMagics[] = {
    {std::span<const uint8_t>(kJp2Codestream), TileCodec::kJp2},
    {std::span<const uint8_t>(kJp2Box), TileCodec::kJp2},
    {std::span<const uint8_t>(kJpegSoi), TileCodec::kJpeg},
};

template <typename T>
T LoadLe(const uint8_t* p) {
  T value{};
  std::memcpy(&value, p, sizeof(T));
  return value;
}

aifocore::Status ParseSisHeader(std::span<const uint8_t> buf, SisHeader* out) {
  if (buf.size() < kSisHeaderSize) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Olympus VSI: file too small for SIS header");
  }
  if (!std::equal(kSisMagic.begin(), kSisMagic.end(), buf.begin())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Olympus VSI: missing SIS\\0 magic at offset 0");
  }
  const uint8_t* p = buf.data();
  out->header_size = LoadLe<uint32_t>(p + 4);
  out->version = LoadLe<uint32_t>(p + 8);
  out->ndim = LoadLe<uint32_t>(p + 12);
  out->ets_offset = LoadLe<uint64_t>(p + 16);
  out->ets_nbytes = LoadLe<uint32_t>(p + 24);
  // p+28: pad u32 (ignored)
  out->tiles_offset = LoadLe<uint64_t>(p + 32);
  out->n_tiles = LoadLe<uint32_t>(p + 40);
  // p+44..p+63: 5x reserved u32 (ignored)
  if (out->header_size != kSisHeaderSize) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Olympus VSI: declared SIS header_size={} disagrees with "
            "observed struct size {}",
            out->header_size, kSisHeaderSize));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status ParseEtsHeader(std::span<const uint8_t> buf, uint64_t offset,
                                EtsHeader* out) {
  if (offset + kEtsPrefixSize + kEtsBackgroundPad > buf.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Olympus VSI: ETS header extends past end of header window");
  }
  const uint8_t* p = buf.data() + offset;
  if (!std::equal(kEtsMagic.begin(), kEtsMagic.end(), p, p + 4)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Olympus VSI: missing ETS\\0 magic at offset {}",
                              offset));
  }
  out->version = LoadLe<uint32_t>(p + 4);
  const uint32_t pixel_raw = LoadLe<uint32_t>(p + 8);
  out->n_channels = LoadLe<uint32_t>(p + 12);
  out->color_space = LoadLe<uint32_t>(p + 16);
  const uint32_t codec_raw = LoadLe<uint32_t>(p + 20);
  out->quality = LoadLe<uint32_t>(p + 24);
  out->tile_w = LoadLe<uint32_t>(p + 28);
  out->tile_h = LoadLe<uint32_t>(p + 32);
  out->tile_d = LoadLe<uint32_t>(p + 36);

  constexpr uint32_t kExpectedPixelUint8 =
      static_cast<uint32_t>(TilePixelType::kUInt8);
  constexpr uint32_t kExpectedPixelUint16 =
      static_cast<uint32_t>(TilePixelType::kUInt16);
  if (pixel_raw != kExpectedPixelUint8 && pixel_raw != kExpectedPixelUint16) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format(
            "Olympus VSI: unsupported pixel_type {} (supported: {} / {}). "
            "Please open an issue at {} with a sample file.",
            pixel_raw, kExpectedPixelUint8, kExpectedPixelUint16, kIssuesUrl));
  }
  out->pixel_type = static_cast<TilePixelType>(pixel_raw);

  // Olympus VS scanners produce 1-channel (single-band fluorescence /
  // grayscale brightfield), 3-channel (RGB brightfield), and rarely
  // 4-channel (RGBA-style) ETS files. The plan builder caps usable
  // channels at 4, so anything outside [1, 4] is rejected up front.
  if (out->n_channels < 1U || out->n_channels > 4U) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format(
            "Olympus VSI: unsupported n_channels {} (supported: 1-4). "
            "Please open an issue at {} with a sample file.",
            out->n_channels, kIssuesUrl));
  }

  constexpr uint32_t kExpectedCodecRaw = static_cast<uint32_t>(TileCodec::kRaw);
  constexpr uint32_t kExpectedCodecJpeg =
      static_cast<uint32_t>(TileCodec::kJpeg);
  constexpr uint32_t kExpectedCodecJp2 = static_cast<uint32_t>(TileCodec::kJp2);
  if (codec_raw != kExpectedCodecRaw && codec_raw != kExpectedCodecJpeg &&
      codec_raw != kExpectedCodecJp2) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format(
            "Olympus VSI: unsupported declared compression {} (supported: "
            "{}, {}, {}). Please open an issue at {} with a sample file.",
            codec_raw, kExpectedCodecRaw, kExpectedCodecJpeg, kExpectedCodecJp2,
            kIssuesUrl));
  }
  out->compression = static_cast<TileCodec>(codec_raw);

  // Per-channel background block: ``bytes_per_sample`` bytes per
  // channel, little-endian on disk. For 8-bit pixels the components
  // are ``0..255``; for 16-bit pixels they are ``0..65535``. The
  // ``background`` slot holds the raw sample value as ``int32_t`` so
  // the plan builder can narrow them uniformly (see
  // ``BackgroundComponentToByte``).
  const size_t bg_offset = offset + kEtsPrefixSize + kEtsBackgroundPad;
  const uint32_t n = std::min<uint32_t>(out->n_channels, 4U);
  const uint32_t bytes_per_sample =
      (out->pixel_type == TilePixelType::kUInt16) ? 2U : 1U;
  if (bg_offset + static_cast<size_t>(n) * bytes_per_sample > buf.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Olympus VSI: background colour block out of bounds");
  }
  for (uint32_t i = 0; i < n; ++i) {
    if (bytes_per_sample == 2U) {
      out->background[i] = static_cast<int32_t>(
          LoadLe<uint16_t>(buf.data() + bg_offset + i * 2));
    } else {
      out->background[i] = static_cast<int32_t>(buf[bg_offset + i]);
    }
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status ParseTileTable(std::span<const uint8_t> table,
                                uint32_t n_tiles, uint32_t ndim,
                                std::vector<TileRecord>* out) {
  if (ndim < kTileRecordMinNdim || ndim > kTileRecordMaxNdim) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Olympus VSI: SIS.ndim={} outside supported range [{}, {}]", ndim,
            kTileRecordMinNdim, kTileRecordMaxNdim));
  }
  const size_t record_size = TileRecordSize(ndim);
  const size_t pair_offset = TileRecordPairOffset(ndim);
  const size_t level_offset = TileRecordLevelOffset(ndim);
  const uint64_t total = static_cast<uint64_t>(n_tiles) * record_size;
  if (table.size() < total) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Olympus VSI: tile-table span ({} B) smaller than n_tiles ({}) "
            "* record size ({})",
            table.size(), n_tiles, record_size));
  }
  out->resize(n_tiles);
  for (uint32_t i = 0; i < n_tiles; ++i) {
    const uint8_t* p = table.data() + i * record_size;
    TileRecord& rec = (*out)[i];
    rec.const_marker = LoadLe<uint32_t>(p + 0);
    rec.x = LoadLe<uint32_t>(p + 4);
    rec.y = LoadLe<uint32_t>(p + 8);
    rec.level = LoadLe<uint32_t>(p + level_offset);
    // Capture the raw non-spatial coordinate slots (slots 2 .. ndim-2),
    // i.e. everything between (x, y) and the trailing level slot. The
    // first such slot sits at byte offset 12. Their semantic role
    // (channel / focal / time) is not stored in the ``.ets`` file; the
    // pyramid builder assigns roles by slot index using the ``.vsi``
    // dimension metadata.
    constexpr size_t kFirstAxisByteOffset = 12;
    rec.axis_count = static_cast<uint32_t>(
        (level_offset - kFirstAxisByteOffset) / sizeof(uint32_t));
    for (uint32_t s = 0; s < rec.axis_count && s < TileRecord::kMaxAxisSlots;
         ++s) {
      rec.axis_slots[s] =
          LoadLe<uint32_t>(p + kFirstAxisByteOffset + s * sizeof(uint32_t));
    }
    rec.offset = LoadLe<uint64_t>(p + pair_offset);
    rec.n_bytes = LoadLe<uint32_t>(p + pair_offset + 8);
    rec.trailing_pad = LoadLe<uint32_t>(p + pair_offset + 12);
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status RunLayoutSelfCheck(uint64_t file_size, const EtsFileData& data,
                                    std::span<const uint8_t> first_tile_probe) {
  // 1) Every (offset, n_bytes) pair must stay in [ets_offset + ets_nbytes,
  //    file_size) so tiles do not overlap the declared ETS body or go past
  //    EOF. We also require offsets > 0.
  uint64_t min_offset = 0;
  bool first = true;
  for (const auto& rec : data.tiles) {
    if (rec.offset == 0) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Olympus VSI: tile record has zero offset");
    }
    if (rec.offset + rec.n_bytes > file_size) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "Olympus VSI: tile extends past EOF (offset={}, n_bytes={}, "
              "file_size={})",
              rec.offset, rec.n_bytes, file_size));
    }
    if (first || rec.offset < min_offset) {
      min_offset = rec.offset;
      first = false;
    }
  }
  if (!first && min_offset < data.sis.ets_offset + data.sis.ets_nbytes) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Olympus VSI: a tile offset ({}) overlaps the declared ETS body "
            "[{}, {})",
            min_offset, data.sis.ets_offset,
            data.sis.ets_offset + data.sis.ets_nbytes));
  }

  // 2) const_marker must be constant and equal SIS.ndim.
  if (!data.tiles.empty()) {
    const uint32_t expected = data.tiles[0].const_marker;
    for (const auto& rec : data.tiles) {
      if (rec.const_marker != expected) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInvalidArgument,
            "Olympus VSI: tile-record marker column is not constant");
      }
    }
    if (expected != data.sis.ndim) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "Olympus VSI: tile-record marker = {} but SIS.ndim = {}",
              expected, data.sis.ndim));
    }
  }

  // 3) First-tile magic must match declared compression when recognizable.
  //    RAW tiles carry no file magic, so the check only applies to the
  //    compressed codecs.
  const bool compressed_codec = data.ets.compression == TileCodec::kJpeg ||
                                data.ets.compression == TileCodec::kJp2;
  if (compressed_codec && !data.tiles.empty() && !first_tile_probe.empty()) {
    const auto sniffed = SniffCodec(first_tile_probe);
    if (sniffed != TileCodec::kUnknown &&
        data.ets.compression != TileCodec::kUnknown &&
        sniffed != data.ets.compression) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Olympus VSI: ETS declares compression={} but "
                                "first tile magic = {}",
                                CodecName(data.ets.compression),
                                CodecName(sniffed)));
    }
  }
  return aifocore::Status::OkStatus();
}

}  // namespace

TileCodec SniffCodec(std::span<const uint8_t> buf) {
  for (const auto& entry : kCodecMagics) {
    if (buf.size() < entry.magic.size())
      continue;
    if (std::equal(entry.magic.begin(), entry.magic.end(), buf.begin())) {
      return entry.codec;
    }
  }
  return TileCodec::kUnknown;
}

std::string_view CodecName(TileCodec codec) {
  switch (codec) {
    case TileCodec::kRaw:
      return "RAW";
    case TileCodec::kJpeg:
      return "JPEG";
    case TileCodec::kJp2:
      return "JP2";
    case TileCodec::kUnknown:
    default:
      return "UNKNOWN";
  }
}

aifocore::Result<EtsFileData> ParseEtsBuffer(std::span<const uint8_t> buffer,
                                             fs::path path) {
  EtsFileData data;
  data.path = std::move(path);
  AIFOCORE_RETURN_IF_ERROR(ParseSisHeader(buffer, &data.sis));
  AIFOCORE_RETURN_IF_ERROR(
      ParseEtsHeader(buffer, data.sis.ets_offset, &data.ets));

  // Locate and bounds-check the tile table inside the buffer. The
  // per-record size is determined by ``ndim`` (see ``TileRecordSize``).
  const size_t record_size = TileRecordSize(data.sis.ndim);
  const uint64_t total_table_bytes =
      static_cast<uint64_t>(data.sis.n_tiles) * record_size;
  if (data.sis.tiles_offset > buffer.size() ||
      data.sis.tiles_offset + total_table_bytes > buffer.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Olympus VSI: tile table [{}, {}) extends past end of buffer "
            "(size={})",
            data.sis.tiles_offset, data.sis.tiles_offset + total_table_bytes,
            buffer.size()));
  }
  const auto table = buffer.subspan(static_cast<size_t>(data.sis.tiles_offset),
                                    static_cast<size_t>(total_table_bytes));
  AIFOCORE_RETURN_IF_ERROR(
      ParseTileTable(table, data.sis.n_tiles, data.sis.ndim, &data.tiles));

  // Build a probe span for the first tile (if any) for the codec check.
  std::span<const uint8_t> first_tile_probe;
  if (!data.tiles.empty()) {
    const auto& first = data.tiles.front();
    if (first.offset < buffer.size()) {
      const size_t bytes_to_eof =
          buffer.size() - static_cast<size_t>(first.offset);
      const size_t probe_bytes = std::min<size_t>(
          {static_cast<size_t>(first.n_bytes), kCodecProbeBytes, bytes_to_eof});
      first_tile_probe =
          buffer.subspan(static_cast<size_t>(first.offset), probe_bytes);
    }
  }
  AIFOCORE_RETURN_IF_ERROR(
      RunLayoutSelfCheck(buffer.size(), data, first_tile_probe));
  return data;
}

aifocore::Result<EtsFileData> ParseEtsFile(const fs::path& path) {
  AIFOCORE_ASSIGN_OR_RETURN(auto file, FileReader::Open(path, "rb"));
  AIFOCORE_ASSIGN_OR_RETURN(const int64_t signed_size, file.GetSize());
  if (signed_size < 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Olympus VSI: GetSize() < 0 for '{}'",
                              path.string()));
  }
  const uint64_t file_size = static_cast<uint64_t>(signed_size);

  // 1) Read the header window (SIS + ETS sub-header + background block) in
  //    a single syscall. Only the first ~200 B are strictly needed, but
  //    reading 8 KiB keeps us forward-compatible with larger backgrounds.
  const size_t header_bytes =
      static_cast<size_t>(std::min<uint64_t>(file_size, kHeaderWindowBytes));
  if (header_bytes < kSisHeaderSize) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Olympus VSI: file too small for SIS header");
  }
  std::vector<uint8_t> header_buf(header_bytes);
  AIFOCORE_RETURN_IF_ERROR(file.Seek(0));
  AIFOCORE_RETURN_IF_ERROR(file.Read(header_buf.data(), header_bytes));

  EtsFileData data;
  data.path = path;
  AIFOCORE_RETURN_IF_ERROR(ParseSisHeader(
      std::span<const uint8_t>(header_buf.data(), header_buf.size()),
      &data.sis));
  AIFOCORE_RETURN_IF_ERROR(ParseEtsHeader(
      std::span<const uint8_t>(header_buf.data(), header_buf.size()),
      data.sis.ets_offset, &data.ets));

  // 2) Targeted read of the tile table near EOF (typically ~1 MB), instead
  //    of slurping the entire ~MB-to-GB file just to reach this region.
  //    Per-record size depends on ``ndim`` (36 B for ndim=4, 40 B for
  //    ndim=5).
  const size_t record_size = TileRecordSize(data.sis.ndim);
  const uint64_t total_table_bytes =
      static_cast<uint64_t>(data.sis.n_tiles) * record_size;
  if (data.sis.tiles_offset > file_size ||
      data.sis.tiles_offset + total_table_bytes > file_size) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Olympus VSI: tile table [{}, {}) extends past EOF (file_size={})",
            data.sis.tiles_offset, data.sis.tiles_offset + total_table_bytes,
            file_size));
  }
  std::vector<uint8_t> table_buf(static_cast<size_t>(total_table_bytes));
  if (total_table_bytes > 0) {
    AIFOCORE_RETURN_IF_ERROR(
        file.Seek(static_cast<int64_t>(data.sis.tiles_offset)));
    AIFOCORE_RETURN_IF_ERROR(
        file.Read(table_buf.data(), static_cast<size_t>(total_table_bytes)));
  }
  AIFOCORE_RETURN_IF_ERROR(ParseTileTable(
      std::span<const uint8_t>(table_buf.data(), table_buf.size()),
      data.sis.n_tiles, data.sis.ndim, &data.tiles));

  // 3) Targeted 16-byte probe of the first tile for the codec self-check.
  std::array<uint8_t, kCodecProbeBytes> probe{};
  std::span<const uint8_t> probe_span;
  if (!data.tiles.empty()) {
    const auto& first = data.tiles.front();
    if (first.offset >= file_size) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "Olympus VSI: first tile offset {} past EOF ({})", first.offset,
              file_size));
    }
    const size_t bytes_to_eof = static_cast<size_t>(file_size - first.offset);
    const size_t probe_bytes = std::min<size_t>(
        {static_cast<size_t>(first.n_bytes), kCodecProbeBytes, bytes_to_eof});
    if (probe_bytes > 0) {
      AIFOCORE_RETURN_IF_ERROR(file.Seek(static_cast<int64_t>(first.offset)));
      AIFOCORE_RETURN_IF_ERROR(file.Read(probe.data(), probe_bytes));
      probe_span = std::span<const uint8_t>(probe.data(), probe_bytes);
    }
  }
  AIFOCORE_RETURN_IF_ERROR(RunLayoutSelfCheck(file_size, data, probe_span));
  return data;
}

}  // namespace fastslide::formats::olympusvsi
