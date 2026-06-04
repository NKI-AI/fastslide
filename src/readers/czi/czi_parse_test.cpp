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

#include "fastslide/readers/czi/czi_parse.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fastslide/readers/czi/czi_level_info.h"

namespace fastslide::czi {
namespace {

// Append a little-endian scalar to `out`.
template <typename T>
void PushLe(std::vector<uint8_t>& out, T value) {
  std::array<uint8_t, sizeof(T)> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(T));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

// Build a 20-byte DimensionEntryDV record.
std::vector<uint8_t> MakeDimensionRecord(char axis, int32_t start, int32_t size,
                                         int32_t stored_size) {
  std::vector<uint8_t> out;
  out.push_back(static_cast<uint8_t>(axis));
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  PushLe<int32_t>(out, start);
  PushLe<int32_t>(out, size);
  PushLe<float>(out, 0.0f);  // start_coordinate (unused)
  PushLe<int32_t>(out, stored_size);
  return out;
}

// Build a 32-byte DirectoryEntryDV fixed header.
std::vector<uint8_t> MakeDirEntryHeader(int32_t pixel_type,
                                        int64_t file_position,
                                        int32_t compression,
                                        int32_t dimension_count) {
  std::vector<uint8_t> out;
  out.push_back('D');
  out.push_back('V');
  PushLe<int32_t>(out, pixel_type);
  PushLe<int64_t>(out, file_position);
  PushLe<int32_t>(out, 0);  // file_part
  PushLe<int32_t>(out, compression);
  // 6 reserved bytes (pyramid_type + reserved), opaque.
  for (int i = 0; i < 6; ++i) {
    out.push_back(0xAB);
  }
  PushLe<int32_t>(out, dimension_count);
  return out;
}

TEST(CziParseTest, ParseDirEntryHeaderReadsFields) {
  const auto bytes = MakeDirEntryHeader(/*pixel_type=*/3,
                                        /*file_position=*/0x1122334455,
                                        /*compression=*/6,
                                        /*dimension_count=*/4);
  ASSERT_EQ(bytes.size(), kDirEntryFixedSize);

  auto header_or = ParseDirEntryHeader(bytes);
  ASSERT_TRUE(header_or.ok()) << header_or.status().ToString();
  const auto& header = *header_or;
  EXPECT_EQ(header.pixel_type, 3);
  EXPECT_EQ(header.file_position, 0x1122334455);
  EXPECT_EQ(header.compression, 6);
  EXPECT_EQ(header.dimension_count, 4);
}

TEST(CziParseTest, ParseDirEntryHeaderRejectsBadSchema) {
  auto bytes = MakeDirEntryHeader(3, 0, 0, 0);
  bytes[0] = 'X';
  EXPECT_FALSE(ParseDirEntryHeader(bytes).ok());
}

TEST(CziParseTest, ParseDirEntryHeaderRejectsShortBuffer) {
  std::vector<uint8_t> bytes(kDirEntryFixedSize - 1, 0);
  EXPECT_FALSE(ParseDirEntryHeader(bytes).ok());
}

TEST(CziParseTest, ParseDimensionRecordReadsAxisAndSizes) {
  const auto bytes = MakeDimensionRecord('X', /*start=*/128, /*size=*/2048,
                                         /*stored_size=*/512);
  ASSERT_EQ(bytes.size(), kDimensionEntrySize);

  auto rec_or = ParseDimensionRecord(bytes);
  ASSERT_TRUE(rec_or.ok()) << rec_or.status().ToString();
  EXPECT_EQ(rec_or->axis, 'X');
  EXPECT_EQ(rec_or->start, 128);
  EXPECT_EQ(rec_or->size, 2048);
  EXPECT_EQ(rec_or->stored_size, 512);
}

TEST(CziParseTest, ParseDimensionRecordReadsZAndTAxes) {
  const auto z_rec = MakeDimensionRecord('Z', /*start=*/3, /*size=*/1,
                                         /*stored_size=*/1);
  auto z_or = ParseDimensionRecord(z_rec);
  ASSERT_TRUE(z_or.ok()) << z_or.status().ToString();
  EXPECT_EQ(z_or->axis, 'Z');
  EXPECT_EQ(z_or->start, 3);

  const auto t_rec = MakeDimensionRecord('T', /*start=*/7, /*size=*/1,
                                         /*stored_size=*/1);
  auto t_or = ParseDimensionRecord(t_rec);
  ASSERT_TRUE(t_or.ok()) << t_or.status().ToString();
  EXPECT_EQ(t_or->axis, 'T');
  EXPECT_EQ(t_or->start, 7);
}

TEST(CziParseTest, SortedUniqueAxisSortsAndDeduplicates) {
  const std::vector<int32_t> values = {2, 0, 2, 1, 0, 3};
  const auto unique = SortedUniqueAxis(values);
  const std::vector<int32_t> expected = {0, 1, 2, 3};
  EXPECT_EQ(unique, expected);
}

TEST(CziParseTest, SortedUniqueAxisHandlesEmptyAndSingle) {
  EXPECT_TRUE(SortedUniqueAxis(std::vector<int32_t>{}).empty());

  const std::vector<int32_t> single = {5, 5, 5};
  const auto unique = SortedUniqueAxis(single);
  ASSERT_EQ(unique.size(), 1u);
  EXPECT_EQ(unique[0], 5);
}

TEST(CziParseTest, DownsampleFromSizesRoundsToNearest) {
  EXPECT_EQ(DownsampleFromSizes(2048, 512), 4);
  EXPECT_EQ(DownsampleFromSizes(1024, 1024), 1);
  // Slightly off ratios still snap to the nearest integer level.
  EXPECT_EQ(DownsampleFromSizes(2050, 512), 4);
  // Degenerate stored size clamps to a valid level.
  EXPECT_EQ(DownsampleFromSizes(2048, 0), 1);
}

TEST(CziParseTest, GroupSubblocksBySceneOrdersAscending) {
  std::vector<CziSubblockInfo> subblocks(4);
  subblocks[0].index = 0;
  subblocks[0].scene = 2;
  subblocks[1].index = 1;
  subblocks[1].scene = 0;
  subblocks[2].index = 2;
  subblocks[2].scene = 2;
  subblocks[3].index = 3;
  subblocks[3].scene = 1;

  const auto groups = GroupSubblocksByScene(subblocks);
  ASSERT_EQ(groups.size(), 3u);
  EXPECT_EQ(groups[0].scene_id, 0);
  EXPECT_EQ(groups[1].scene_id, 1);
  EXPECT_EQ(groups[2].scene_id, 2);

  // Scene 2 keeps both of its subblocks in original order.
  ASSERT_EQ(groups[2].subblock_indices.size(), 2u);
  EXPECT_EQ(groups[2].subblock_indices[0], 0u);
  EXPECT_EQ(groups[2].subblock_indices[1], 2u);
}

TEST(CziParseTest, SubblockFixedHeaderLengthCommonCaseIs288) {
  // 0..10 dimensions: 16 + (32 + n*20) <= 256, so data section clamps to 256
  // and the total is 32 (segment header) + 256 = 288, matching the ZISRAW
  // minimum-sized subblock data section.
  EXPECT_EQ(SubblockFixedHeaderLength(0), 288u);
  EXPECT_EQ(SubblockFixedHeaderLength(3), 288u);
  EXPECT_EQ(SubblockFixedHeaderLength(10), 288u);
}

TEST(CziParseTest, SubblockFixedHeaderLengthGrowsForHighDimEntries) {
  // 11 dims: inline entry = 32 + 11*20 = 252; 16 + 252 = 268 > 256, so the
  // data section is 268 and the total is 32 + 268 = 300.
  EXPECT_EQ(SubblockFixedHeaderLength(11), 300u);
  EXPECT_EQ(SubblockFixedHeaderLength(12), 320u);
}

TEST(CziParseTest, ParseZstd1PayloadHandlesHeaderVariants) {
  // header_len == 1: no options, payload starts at byte 1.
  const std::vector<uint8_t> only_len = {1, 0xAA, 0xBB};
  auto p1 = ParseZstd1Payload(only_len);
  ASSERT_TRUE(p1.ok()) << p1.status().ToString();
  EXPECT_FALSE(p1->do_hilo);
  EXPECT_EQ(p1->payload.size(), 2u);

  // header_len == 3, chunk type 1, hilo flag set.
  const std::vector<uint8_t> with_hilo = {3, 1, 1, 0xCC};
  auto p3 = ParseZstd1Payload(with_hilo);
  ASSERT_TRUE(p3.ok()) << p3.status().ToString();
  EXPECT_TRUE(p3->do_hilo);
  EXPECT_EQ(p3->payload.size(), 1u);

  // Unexpected chunk type is rejected.
  const std::vector<uint8_t> bad_chunk = {3, 2, 0, 0};
  EXPECT_FALSE(ParseZstd1Payload(bad_chunk).ok());
}

TEST(CziParseTest, UnpackHiLo16InterleavesHalves) {
  // Lo bytes [10, 11] in the first half, hi bytes [20, 21] in the second.
  const std::vector<uint8_t> packed = {10, 11, 20, 21};
  auto out = UnpackHiLo16(packed);
  ASSERT_TRUE(out.ok()) << out.status().ToString();
  const std::vector<uint8_t> expected = {10, 20, 11, 21};
  EXPECT_EQ(*out, expected);

  // Odd byte count is rejected.
  const std::vector<uint8_t> odd = {1, 2, 3};
  EXPECT_FALSE(UnpackHiLo16(odd).ok());
}

TEST(CziParseTest, GroupSubblocksBySceneSingleSceneAllTogether) {
  std::vector<CziSubblockInfo> subblocks(3);
  for (uint32_t i = 0; i < subblocks.size(); ++i) {
    subblocks[i].index = i;
    subblocks[i].scene = 0;
  }
  const auto groups = GroupSubblocksByScene(subblocks);
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].scene_id, 0);
  EXPECT_EQ(groups[0].subblock_indices.size(), 3u);
}

}  // namespace
}  // namespace fastslide::czi
