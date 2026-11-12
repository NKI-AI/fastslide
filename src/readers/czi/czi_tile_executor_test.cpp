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

#include "fastslide/readers/czi/czi_tile_executor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/czi/czi.h"
#include "fastslide/readers/czi/czi_exec_context.h"
#include "fastslide/readers/czi/czi_level_info.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {
namespace {

CziSubblockInfo Subblock(uint32_t index, int32_t m) {
  CziSubblockInfo sb;
  sb.index = index;
  sb.w = 256;
  sb.h = 256;
  sb.m = m;
  return sb;
}

core::TilePlan PlanOver(const std::vector<uint32_t>& subblock_indices) {
  core::TilePlan plan;
  for (const uint32_t index : subblock_indices) {
    core::TileReadOp op{};
    op.source_id = index;
    plan.operations.push_back(op);
  }
  return plan;
}

// Subblock indices of the plan's operations, in the order they are painted.
std::vector<uint32_t> Painted(const std::vector<CziSubblockInfo>& subblocks,
                              const std::vector<uint32_t>& plan_subblocks,
                              bool first_writer_wins) {
  const CziExecContext context("synthetic.czi", subblocks, nullptr,
                               /*is_spectral=*/false, JpegDecodeMode::kFast);
  const core::TilePlan plan = PlanOver(plan_subblocks);
  std::vector<uint32_t> out;
  for (const uint32_t i :
       CziTileExecutor::PaintOrder(plan, context, first_writer_wins)) {
    out.push_back(plan.operations[i].source_id);
  }
  return out;
}

using Order = std::vector<uint32_t>;

// libCZI draws ascending by M index, each tile overwriting the ones before:
// the highest M index ends up on top.
TEST(CziPaintOrderTest, HighestMosaicIndexPaintsLastWhenTilesOverwrite) {
  const std::vector<CziSubblockInfo> subblocks = {
      Subblock(0, 2), Subblock(1, 0), Subblock(2, 1)};
  EXPECT_EQ(Painted(subblocks, {0, 1, 2}, /*first_writer_wins=*/false),
            (Order{1, 2, 0}));
}

TEST(CziPaintOrderTest, HighestMosaicIndexPaintsFirstWhenFirstWriterWins) {
  const std::vector<CziSubblockInfo> subblocks = {
      Subblock(0, 2), Subblock(1, 0), Subblock(2, 1)};
  EXPECT_EQ(Painted(subblocks, {0, 1, 2}, /*first_writer_wins=*/true),
            (Order{0, 2, 1}));
}

TEST(CziPaintOrderTest, SubblocksWithoutMosaicIndexGoBelowTheRest) {
  const std::vector<CziSubblockInfo> subblocks = {
      Subblock(0, 0), Subblock(1, kCziNoMosaicIndex), Subblock(2, 3)};
  EXPECT_EQ(Painted(subblocks, {0, 1, 2}, /*first_writer_wins=*/false),
            (Order{1, 0, 2}));
}

TEST(CziPaintOrderTest, TiesGoToTheLaterSubblockInTheDirectory) {
  const std::vector<CziSubblockInfo> subblocks = {
      Subblock(0, kCziNoMosaicIndex), Subblock(1, kCziNoMosaicIndex),
      Subblock(2, kCziNoMosaicIndex)};
  EXPECT_EQ(Painted(subblocks, {2, 0, 1}, /*first_writer_wins=*/false),
            (Order{0, 1, 2}));
  EXPECT_EQ(Painted(subblocks, {2, 0, 1}, /*first_writer_wins=*/true),
            (Order{2, 1, 0}));
}

TEST(CziPaintOrderTest, OrderDoesNotDependOnPlanOrder) {
  const std::vector<CziSubblockInfo> subblocks = {
      Subblock(0, 1), Subblock(1, 1), Subblock(2, 0), Subblock(3, 5)};
  for (const bool first_writer_wins : {false, true}) {
    EXPECT_EQ(Painted(subblocks, {0, 1, 2, 3}, first_writer_wins),
              Painted(subblocks, {3, 1, 2, 0}, first_writer_wins));
  }
}

// A subblock that cannot be read fails the read instead of quietly leaving
// background where its pixels belong.
TEST(CziExecutePlanTest, ASubblockThatFailsToDecodeFailsTheRead) {
  const std::vector<CziSubblockInfo> subblocks = {
      Subblock(0, kCziNoMosaicIndex), Subblock(1, kCziNoMosaicIndex)};
  const CziExecContext context("/nonexistent/fastslide-missing.czi", subblocks,
                               nullptr, /*is_spectral=*/false,
                               JpegDecodeMode::kFast);
  core::TilePlan plan = PlanOver({0, 1});
  for (auto& op : plan.operations) {
    op.transform.source = {0.0, 0.0, 256, 256};
    op.transform.dest = {0.0, 0.0, 256, 256};
  }
  runtime::Canvas::Config config;
  config.dimensions = {256, 256};
  config.channels = 3;
  config.data_type = DataType::kUInt8;
  config.background = runtime::Canvas::BackgroundColor(255, 255, 255);
  runtime::Canvas canvas(config);

  const auto status = CziTileExecutor::ExecutePlan(plan, context, canvas);

  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("CZI subblock"), std::string::npos)
      << status.message();
}

// Guarded end-to-end test. Set FASTSLIDE_CZI_TEST_FILE to the path of a real
// CZI mosaic whose tiles overlap. Skipped (not failed) when the variable is
// unset or the file is missing, so the suite stays hermetic in CI.
//
// The same read must return the same pixels every time, whatever order the
// subblocks finish decoding in.
TEST(CziDeterminismTest, RepeatedReadsReturnTheSamePixels) {
  const char* path = std::getenv("FASTSLIDE_CZI_TEST_FILE");
  if (path == nullptr || !std::filesystem::exists(path)) {
    GTEST_SKIP() << "Set FASTSLIDE_CZI_TEST_FILE to a .czi mosaic to run this.";
  }

  auto reader_or = CziReader::Create(path);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  auto& reader = *reader_or;

  const int level_count = reader->GetLevelCount();
  ASSERT_GT(level_count, 0);
  for (const int level : {0, level_count / 2}) {
    auto info_or = reader->GetLevelInfo(level);
    ASSERT_TRUE(info_or.ok());
    const auto dims = info_or->dimensions;
    const uint32_t w = std::min<uint32_t>(2048, dims[0]);
    const uint32_t h = std::min<uint32_t>(2048, dims[1]);
    RegionSpec region;
    region.top_left = ImageCoordinate{(dims[0] - w) / 2, (dims[1] - h) / 2};
    region.size = ImageDimensions{w, h};
    region.level = level;

    auto first_or = reader->ReadRegion(region);
    ASSERT_TRUE(first_or.ok()) << first_or.status().message();
    const std::vector<uint8_t> first = first_or->GetDataVector();
    for (int i = 0; i < 10; ++i) {
      auto again_or = reader->ReadRegion(region);
      ASSERT_TRUE(again_or.ok()) << again_or.status().message();
      EXPECT_EQ(again_or->GetDataVector(), first)
          << "level " << level << ", read " << i + 1;
    }
  }
}

}  // namespace
}  // namespace fastslide
