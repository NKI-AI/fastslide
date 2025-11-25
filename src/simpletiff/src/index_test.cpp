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

#include "simpletiff/index.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace simpletiff {
namespace {

TEST(IndexTest, DefaultConstruction) {
  TiffIndex index;
  EXPECT_FALSE(index.IsBigTiff());
  EXPECT_TRUE(index.IsLittleEndian());
  EXPECT_EQ(index.FileSize(), 0);
  EXPECT_EQ(index.NumPages(), 0);
}

TEST(IndexTest, SpanViews) {
  TiffIndex index;

  // Add some test data to arenas using builder methods
  std::vector<uint64_t> test_offsets = {100, 200, 300, 400, 500};
  std::vector<uint64_t> test_bytecounts = {10, 20, 30, 40, 50};

  SpanU64 span1 = index.AppendOffsets(test_offsets);
  SpanU64 span2 = index.AppendBytecounts(test_bytecounts);

  // Test first 3 offsets
  SpanU64 partial_span1{0, 3};
  auto offsets = index.Offsets(partial_span1);
  EXPECT_EQ(offsets.size(), 3);
  EXPECT_EQ(offsets[0], 100);
  EXPECT_EQ(offsets[1], 200);
  EXPECT_EQ(offsets[2], 300);

  // Test middle 2 bytecounts
  SpanU64 partial_span2{2, 2};
  auto bytecounts = index.Bytecounts(partial_span2);
  EXPECT_EQ(bytecounts.size(), 2);
  EXPECT_EQ(bytecounts[0], 30);
  EXPECT_EQ(bytecounts[1], 40);
}

TEST(IndexTest, PageHeader) {
  PageHeader header;
  EXPECT_EQ(header.width, 0);
  EXPECT_EQ(header.height, 0);
  EXPECT_EQ(header.storage, Storage::kUnknown);
  EXPECT_EQ(header.payload_id, 0);
}

TEST(IndexTest, TilesRec) {
  TilesRec tiles;
  EXPECT_EQ(tiles.tile_w, 0);
  EXPECT_EQ(tiles.tile_h, 0);
  EXPECT_EQ(tiles.tiles_x, 0);
  EXPECT_EQ(tiles.tiles_y, 0);
  EXPECT_EQ(tiles.offsets.start, 0);
  EXPECT_EQ(tiles.offsets.count, 0);
}

TEST(IndexTest, StripsRec) {
  StripsRec strips;
  EXPECT_EQ(strips.rows_per_strip, 0);
  EXPECT_EQ(strips.offsets.start, 0);
  EXPECT_EQ(strips.offsets.count, 0);
}

TEST(IndexTest, SingleJpegRec) {
  SingleJpegRec single;
  EXPECT_EQ(single.offset, 0);
  EXPECT_EQ(single.length, 0);
}

TEST(IndexTest, Roi) {
  Roi roi{10, 20, 100, 200};
  EXPECT_EQ(roi.x, 10);
  EXPECT_EQ(roi.y, 20);
  EXPECT_EQ(roi.width, 100);
  EXPECT_EQ(roi.height, 200);
}

}  // namespace
}  // namespace simpletiff
