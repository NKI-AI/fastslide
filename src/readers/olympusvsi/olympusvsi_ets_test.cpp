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

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "gtest/gtest.h"

namespace fastslide::formats::olympusvsi {
namespace {

template <typename T>
void Append(std::vector<uint8_t>* out, T value) {
  const size_t off = out->size();
  out->resize(off + sizeof(T));
  std::memcpy(out->data() + off, &value, sizeof(T));
}

void WriteAt(std::vector<uint8_t>* out, size_t offset,
             std::span<const uint8_t> bytes) {
  if (offset + bytes.size() > out->size()) {
    out->resize(offset + bytes.size());
  }
  std::memcpy(out->data() + offset, bytes.data(), bytes.size());
}

template <typename T>
void WriteAt(std::vector<uint8_t>* out, size_t offset, T value) {
  if (offset + sizeof(T) > out->size()) {
    out->resize(offset + sizeof(T));
  }
  std::memcpy(out->data() + offset, &value, sizeof(T));
}

struct FakeFile {
  std::vector<uint8_t> bytes;
  uint64_t ets_offset = 0;
  uint64_t tiles_offset = 0;
  uint64_t tile0_offset = 0;
};

// Synthetic ETS file with `n_tiles_x * n_tiles_y` tiles at one level.
//
// ``ndim`` controls the per-tile record width: slot 0 is x, slot 1 is y,
// the last slot is the pyramid level, and slots 2 .. ndim-2 are the
// non-spatial (channel / focal / time) axes. ``axis_values`` supplies the
// per-tile values for those in-between slots (indexed by tile, then by
// axis-slot); empty entries default to all-zero (the principal slice).
FakeFile BuildFakeEtsFile(
    uint32_t n_tiles_x, uint32_t n_tiles_y, uint32_t tile_size = 64,
    TileCodec codec = TileCodec::kJp2, uint32_t ndim = 4,
    const std::vector<std::vector<uint32_t>>& axis_values = {}) {
  FakeFile f;
  f.bytes.reserve(4096);

  constexpr std::array<uint8_t, 4> kSisMagic = {'S', 'I', 'S', 0};
  constexpr std::array<uint8_t, 4> kEtsMagic = {'E', 'T', 'S', 0};
  constexpr std::array<uint8_t, 4> kJp2Magic = {0xFF, 0x4F, 0xFF, 0x51};
  constexpr std::array<uint8_t, 4> kJpegMagic = {0xFF, 0xD8, 0xFF, 0xE0};
  const std::array<uint8_t, 4>& tile_magic =
      (codec == TileCodec::kJpeg) ? kJpegMagic : kJp2Magic;

  // ---- SIS header (64 bytes) ----
  f.bytes.resize(64, 0);
  std::memcpy(f.bytes.data() + 0, kSisMagic.data(), 4);
  WriteAt<uint32_t>(&f.bytes, 4, 64);  // header_size
  WriteAt<uint32_t>(&f.bytes, 8, 1);   // version
  WriteAt<uint32_t>(&f.bytes, 12, ndim);

  // Patch ets_offset / tiles_offset / n_tiles after we know the layout.
  // ETS header lives right after the SIS header.
  f.ets_offset = 64;

  // ---- ETS header (40 byte prefix + 17*4 pad + 4 bytes background) ----
  WriteAt(&f.bytes, f.ets_offset, std::span<const uint8_t>(kEtsMagic));
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 4, 1);  // version
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 8,
                    static_cast<uint32_t>(TilePixelType::kUInt8));
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 12, 3);  // n_channels
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 16, 0);  // color_space
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 20, static_cast<uint32_t>(codec));
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 24, 80);   // quality
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 28, 512);  // tile_w
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 32, 512);  // tile_h
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 36, 1);    // tile_d
  // 17 reserved u32 (zero) then background block of n_channels bytes.
  const size_t bg_offset = f.ets_offset + 40 + 17 * 4;
  if (bg_offset + 3 > f.bytes.size())
    f.bytes.resize(bg_offset + 3, 0);
  f.bytes[bg_offset + 0] = 255;
  f.bytes[bg_offset + 1] = 200;
  f.bytes[bg_offset + 2] = 100;

  // Declared ETS body length: from ets_offset up to the first tile.
  const uint64_t ets_block_end = bg_offset + 3;

  // ---- Tile table ----
  const uint32_t n_tiles = n_tiles_x * n_tiles_y;
  const uint64_t tiles_offset = (ets_block_end + 7) & ~uint64_t{7};
  f.tiles_offset = tiles_offset;
  WriteAt<uint64_t>(&f.bytes, 16, f.ets_offset);
  WriteAt<uint32_t>(&f.bytes, 24,
                    static_cast<uint32_t>(ets_block_end - f.ets_offset));
  WriteAt<uint64_t>(&f.bytes, 32, f.tiles_offset);
  WriteAt<uint32_t>(&f.bytes, 40, n_tiles);

  // record_size = 4 (const_marker) + 4*ndim (slots) + 16 (offset/size/pad).
  const size_t record_size = 4 + 4 * static_cast<size_t>(ndim) + 16;
  const size_t level_off = 4 + 4 * static_cast<size_t>(ndim - 1);
  const size_t pair_off = 4 + 4 * static_cast<size_t>(ndim);
  f.bytes.resize(f.tiles_offset + static_cast<size_t>(n_tiles) * record_size,
                 0);

  // Tile payload area follows the tile table.
  uint64_t tile_data_offset =
      f.tiles_offset + static_cast<size_t>(n_tiles) * record_size;
  f.tile0_offset = tile_data_offset;

  for (uint32_t i = 0; i < n_tiles; ++i) {
    const uint32_t x = i % n_tiles_x;
    const uint32_t y = i / n_tiles_x;
    const size_t rec_offset = f.tiles_offset + i * record_size;
    WriteAt<uint32_t>(&f.bytes, rec_offset + 0, ndim);  // const_marker == ndim
    WriteAt<uint32_t>(&f.bytes, rec_offset + 4, x);
    WriteAt<uint32_t>(&f.bytes, rec_offset + 8, y);
    // Non-spatial axis slots (slots 2 .. ndim-2), starting at byte 12.
    if (i < axis_values.size()) {
      for (size_t s = 0; s < axis_values[i].size(); ++s) {
        WriteAt<uint32_t>(&f.bytes, rec_offset + 12 + s * 4, axis_values[i][s]);
      }
    }
    WriteAt<uint32_t>(&f.bytes, rec_offset + level_off, 0U);  // level
    WriteAt<uint64_t>(&f.bytes, rec_offset + pair_off, tile_data_offset);
    WriteAt<uint32_t>(&f.bytes, rec_offset + pair_off + 8, tile_size);
    WriteAt<uint32_t>(&f.bytes, rec_offset + pair_off + 12, 0);

    // Tile payload: codec-appropriate magic prefix, then pad.
    if (tile_data_offset + tile_size > f.bytes.size()) {
      f.bytes.resize(tile_data_offset + tile_size, 0);
    }
    std::memcpy(f.bytes.data() + tile_data_offset, tile_magic.data(), 4);
    tile_data_offset += tile_size;
  }
  return f;
}

TEST(OlympusVsiEtsTest, ParsesValidFile) {
  const FakeFile f = BuildFakeEtsFile(/*nx=*/2, /*ny=*/3);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  ASSERT_TRUE(result.ok()) << result.status().message();
  const EtsFileData& data = *result;

  EXPECT_EQ(data.sis.header_size, 64U);
  EXPECT_EQ(data.sis.ndim, 4U);
  EXPECT_EQ(data.sis.ets_offset, f.ets_offset);
  EXPECT_EQ(data.sis.tiles_offset, f.tiles_offset);
  EXPECT_EQ(data.sis.n_tiles, 6U);

  EXPECT_EQ(data.ets.tile_w, 512U);
  EXPECT_EQ(data.ets.tile_h, 512U);
  EXPECT_EQ(data.ets.n_channels, 3U);
  EXPECT_EQ(data.ets.compression, TileCodec::kJp2);
  EXPECT_EQ(data.ets.background[0], 255);
  EXPECT_EQ(data.ets.background[1], 200);
  EXPECT_EQ(data.ets.background[2], 100);

  ASSERT_EQ(data.tiles.size(), 6U);
  for (uint32_t i = 0; i < data.tiles.size(); ++i) {
    EXPECT_EQ(data.tiles[i].const_marker, data.sis.ndim);
    EXPECT_EQ(data.tiles[i].n_bytes, 64U);
    // ndim=4: a single non-spatial slot (slot 2) sits between (x, y) and
    // the trailing level slot; here it is 0 for every tile.
    EXPECT_EQ(data.tiles[i].axis_count, 1U);
    EXPECT_EQ(data.tiles[i].AxisAt(2), 0U);
    EXPECT_EQ(data.tiles[i].level, 0U);
  }
  // First tile's grid (x, y) is (0, 0); offset should equal tile0_offset.
  EXPECT_EQ(data.tiles[0].offset, f.tile0_offset);
}

TEST(OlympusVsiEtsTest, ParsesMultipleAxisSlots) {
  // ndim=6: three non-spatial slots (slots 2, 3, 4) sit between (x, y) and
  // the trailing level slot. Give the single tile distinct slot values and
  // confirm they are read back in on-disk order via AxisAt().
  const FakeFile f = BuildFakeEtsFile(
      /*nx=*/1, /*ny=*/1, /*tile_size=*/64, TileCodec::kJp2, /*ndim=*/6,
      /*axis_values=*/{{7U, 3U, 5U}});
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  ASSERT_TRUE(result.ok()) << result.status().message();
  const EtsFileData& data = *result;

  EXPECT_EQ(data.sis.ndim, 6U);
  ASSERT_EQ(data.tiles.size(), 1U);
  const TileRecord& rec = data.tiles[0];
  EXPECT_EQ(rec.axis_count, 3U);
  EXPECT_EQ(rec.AxisAt(2), 7U);
  EXPECT_EQ(rec.AxisAt(3), 3U);
  EXPECT_EQ(rec.AxisAt(4), 5U);
  // Out-of-range slots return 0.
  EXPECT_EQ(rec.AxisAt(1), 0U);
  EXPECT_EQ(rec.AxisAt(5), 0U);
  EXPECT_EQ(rec.level, 0U);
}

TEST(OlympusVsiEtsTest, RejectsBadSisMagic) {
  FakeFile f = BuildFakeEtsFile(2, 2);
  f.bytes[0] = 'X';  // corrupt SIS magic
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  EXPECT_FALSE(result.ok());
}

TEST(OlympusVsiEtsTest, RejectsBadEtsMagic) {
  FakeFile f = BuildFakeEtsFile(2, 2);
  f.bytes[f.ets_offset] = 'X';  // corrupt ETS magic
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  EXPECT_FALSE(result.ok());
}

TEST(OlympusVsiEtsTest, RejectsTileExtendingPastEof) {
  FakeFile f = BuildFakeEtsFile(1, 1);
  // Bump the last tile's n_bytes well past EOF.
  const size_t rec_offset = f.tiles_offset + 0 * 36;
  WriteAt<uint32_t>(&f.bytes, rec_offset + 28, 1u << 30);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  EXPECT_FALSE(result.ok());
}

TEST(OlympusVsiEtsTest, RejectsInconsistentConstMarker) {
  FakeFile f = BuildFakeEtsFile(2, 1);
  const size_t rec_offset = f.tiles_offset + 1 * 36;
  WriteAt<uint32_t>(&f.bytes, rec_offset + 0, 999u);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  EXPECT_FALSE(result.ok());
}

TEST(OlympusVsiEtsTest, SniffsJp2Magic) {
  const uint8_t jp2[] = {0xFF, 0x4F, 0xFF, 0x51, 0, 0};
  const uint8_t junk[] = {0x00, 0x11, 0x22, 0x33};
  EXPECT_EQ(SniffCodec(jp2), TileCodec::kJp2);
  EXPECT_EQ(SniffCodec(junk), TileCodec::kUnknown);
}

TEST(OlympusVsiEtsTest, SniffsJpegMagic) {
  const uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0};
  EXPECT_EQ(SniffCodec(jpeg), TileCodec::kJpeg);
}

TEST(OlympusVsiEtsTest, ParsesJpegCompressedFile) {
  const FakeFile f =
      BuildFakeEtsFile(/*nx=*/2, /*ny=*/2, /*tile_size=*/64, TileCodec::kJpeg);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result->ets.compression, TileCodec::kJpeg);
}

TEST(OlympusVsiEtsTest, RejectsUnsupportedPixelType) {
  FakeFile f = BuildFakeEtsFile(1, 1);
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 8, 5U);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  EXPECT_FALSE(result.ok());
}

TEST(OlympusVsiEtsTest, AcceptsUInt16PixelType) {
  // ``USHORT`` (4) is the 16-bit fluorescence sample type. Override
  // pixel_type AND n_channels so the background block is read as two
  // bytes per component, mirroring real Olympus VSI fluorescence
  // sub-stacks.
  FakeFile f = BuildFakeEtsFile(1, 1);
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 8,
                    static_cast<uint32_t>(TilePixelType::kUInt16));
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 12, 1U);  // single channel
  // Write a 16-bit background sample at the start of the background
  // block: 0xCAFE little-endian.
  const size_t bg_offset = f.ets_offset + 40 + 17 * 4;
  f.bytes[bg_offset + 0] = 0xFE;
  f.bytes[bg_offset + 1] = 0xCA;
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result->ets.pixel_type, TilePixelType::kUInt16);
  EXPECT_EQ(result->ets.n_channels, 1U);
  EXPECT_EQ(result->ets.background[0], 0xCAFE);
}

TEST(OlympusVsiEtsTest, RejectsUnsupportedChannelCount) {
  // 5+ channels are not supported (plan builder caps usable channels
  // at 4); zero channels is malformed.
  FakeFile f1 = BuildFakeEtsFile(1, 1);
  WriteAt<uint32_t>(&f1.bytes, f1.ets_offset + 12, 5U);
  EXPECT_FALSE(
      ParseEtsBuffer(std::span<const uint8_t>(f1.bytes.data(), f1.bytes.size()))
          .ok());

  FakeFile f0 = BuildFakeEtsFile(1, 1);
  WriteAt<uint32_t>(&f0.bytes, f0.ets_offset + 12, 0U);
  EXPECT_FALSE(
      ParseEtsBuffer(std::span<const uint8_t>(f0.bytes.data(), f0.bytes.size()))
          .ok());
}

TEST(OlympusVsiEtsTest, AcceptsSingleChannel) {
  // Single-channel fluorescence / grayscale brightfield is parsed but
  // the BuildPyramid in olympusvsi.cpp will only materialise channel 0.
  FakeFile f = BuildFakeEtsFile(1, 1);
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 12, 1U);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result->ets.n_channels, 1U);
}

TEST(OlympusVsiEtsTest, AcceptsFourChannel) {
  FakeFile f = BuildFakeEtsFile(1, 1);
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 12, 4U);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result->ets.n_channels, 4U);
}

TEST(OlympusVsiEtsTest, RejectsUnsupportedDeclaredCompression) {
  FakeFile f = BuildFakeEtsFile(1, 1);
  // 8 is Olympus FORMAT_PNG, which this reader does not support.
  WriteAt<uint32_t>(&f.bytes, f.ets_offset + 20, 8U);
  auto result =
      ParseEtsBuffer(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
  EXPECT_FALSE(result.ok());
}

}  // namespace
}  // namespace fastslide::formats::olympusvsi
