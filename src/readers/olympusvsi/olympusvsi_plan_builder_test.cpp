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

#include "fastslide/readers/olympusvsi/olympusvsi_plan_builder.h"

#include <cstdint>
#include <set>
#include <span>
#include <vector>

#include "fastslide/core/tile_request.h"
#include "fastslide/readers/olympusvsi/olympusvsi_ets.h"
#include "fastslide/readers/olympusvsi/olympusvsi_level_info.h"
#include "gtest/gtest.h"

namespace fastslide::formats::olympusvsi {
namespace {

OlympusVsiLevelInfo MakeLevel(int level, uint32_t cols, uint32_t rows,
                              uint32_t tile_w = 256, uint32_t tile_h = 256,
                              bool drop_corner = false) {
  OlympusVsiLevelInfo info;
  info.level = level;
  info.tile_w = tile_w;
  info.tile_h = tile_h;
  info.grid_cols = cols;
  info.grid_rows = rows;
  info.size = {cols * tile_w, rows * tile_h};
  // Default: logical image fills the whole grid (no sub-tile trimming).
  info.reported_size = info.size;
  info.downsample = (level == 0) ? 1.0 : static_cast<double>(1 << level);
  uint64_t fake_offset = 1024;
  for (uint32_t y = 0; y < rows; ++y) {
    for (uint32_t x = 0; x < cols; ++x) {
      if (drop_corner && x == 0 && y == 0) {
        continue;  // simulate a missing-cell hole
      }
      info.tile_map[OlympusVsiLevelInfo::PackKey(x, y)] =
          LevelTileEntry{fake_offset, 256};
      fake_offset += 256;
    }
  }
  return info;
}

core::TileRequest MakeRegionRequest(int level, double x, double y, double w,
                                    double h) {
  core::TileRequest req;
  req.level = level;
  req.region_bounds = core::FractionalRegionBounds{x, y, w, h};
  return req;
}

EtsHeader MakeRgbEts(uint32_t n_channels = 3) {
  EtsHeader ets;
  ets.n_channels = n_channels;
  ets.background = {255, 200, 100, 0};
  return ets;
}

TEST(OlympusVsiPlanBuilderTest, FullLevelHasEveryTile) {
  const auto level = MakeLevel(/*level=*/0, /*cols=*/3, /*rows=*/2);
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};

  const auto req = MakeRegionRequest(0, 0, 0, 3 * 256, 2 * 256);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  const auto& plan = *plan_or;

  EXPECT_EQ(plan.operations.size(), 6U);
  EXPECT_EQ(plan.actual_region.size[0], 3U * 256);
  EXPECT_EQ(plan.actual_region.size[1], 2U * 256);
  EXPECT_EQ(plan.output.channels, 3U);
  EXPECT_EQ(plan.output.dimensions[0], 3U * 256);
}

TEST(OlympusVsiPlanBuilderTest, PartialRegionPicksIntersectingTiles) {
  const auto level = MakeLevel(0, 4, 4);  // 1024x1024 image, 256-px tiles
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};

  // A 200x200 region centred on the seam between tiles (2,1) and (2,2).
  const auto req = MakeRegionRequest(0, 600, 400, 200, 200);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  const auto& plan = *plan_or;

  // (2,1) and (3,1) on the top row, (2,2) and (3,2) on the bottom row.
  EXPECT_EQ(plan.operations.size(), 4U);
  EXPECT_EQ(plan.actual_region.size[0], 200U);
  EXPECT_EQ(plan.actual_region.size[1], 200U);
  // Each op's dest must fit inside the 200x200 output.
  for (const auto& op : plan.operations) {
    EXPECT_LE(op.transform.dest.x + op.transform.dest.width, 200U);
    EXPECT_LE(op.transform.dest.y + op.transform.dest.height, 200U);
  }
}

TEST(OlympusVsiPlanBuilderTest, OutOfBoundsRegionProducesNoOps) {
  const auto level = MakeLevel(0, 2, 2);  // 512x512
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};

  const auto req = MakeRegionRequest(0, 2000, 2000, 100, 100);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  ASSERT_TRUE(plan_or.ok());
  EXPECT_TRUE(plan_or->operations.empty());
}

TEST(OlympusVsiPlanBuilderTest, ClampsRegionToLevelExtents) {
  const auto level = MakeLevel(0, 2, 2);  // 512x512
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};

  // Request reaches past the right and bottom edges; planner should clamp
  // the actual_region instead of asking for non-existent pixels.
  const auto req = MakeRegionRequest(0, 400, 400, 1000, 1000);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  ASSERT_TRUE(plan_or.ok());
  EXPECT_EQ(plan_or->actual_region.size[0], 512U - 400U);
  EXPECT_EQ(plan_or->actual_region.size[1], 512U - 400U);
}

TEST(OlympusVsiPlanBuilderTest, ClampsRegionToSubTileReportedBoundary) {
  // A 2x2 tile grid (512x512 on disk) whose true image boundary is the
  // sub-tile 400x300 rectangle from the .vsi boundary-rect tag (2053). A
  // full-image request must clamp to the logical 400x300, not the grid.
  auto level = MakeLevel(0, 2, 2);  // size = {512, 512}
  level.reported_size = {400, 300};
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};

  const auto req = MakeRegionRequest(0, 0, 0, 512, 512);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  const auto& plan = *plan_or;

  EXPECT_EQ(plan.actual_region.size[0], 400U);
  EXPECT_EQ(plan.actual_region.size[1], 300U);
  EXPECT_EQ(plan.output.dimensions[0], 400U);
  EXPECT_EQ(plan.output.dimensions[1], 300U);
  // No painted pixel may land outside the logical 400x300 image.
  for (const auto& op : plan.operations) {
    EXPECT_LE(op.transform.dest.x + op.transform.dest.width, 400U);
    EXPECT_LE(op.transform.dest.y + op.transform.dest.height, 300U);
  }
}

TEST(OlympusVsiPlanBuilderTest, MissingCellsAreSkipped) {
  const auto level = MakeLevel(0, 2, 2, 256, 256, /*drop_corner=*/true);
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};

  const auto req = MakeRegionRequest(0, 0, 0, 512, 512);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  ASSERT_TRUE(plan_or.ok());
  // The (0,0) tile is missing -> only 3 ops are emitted.
  EXPECT_EQ(plan_or->operations.size(), 3U);
}

TEST(OlympusVsiPlanBuilderTest, OutputSpecFromEtsHeader) {
  const auto level = MakeLevel(0, 1, 1);
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};
  EtsHeader ets = MakeRgbEts(3);

  const auto req = MakeRegionRequest(0, 0, 0, 256, 256);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, ets);
  ASSERT_TRUE(plan_or.ok());
  const auto& plan = *plan_or;
  EXPECT_EQ(plan.output.channels, 3U);
  ASSERT_EQ(plan.output.channel_indices.size(), 3U);
  EXPECT_EQ(plan.output.channel_indices[0], 0U);
  EXPECT_EQ(plan.output.channel_indices[2], 2U);
  EXPECT_EQ(plan.output.background.r, 255);
  EXPECT_EQ(plan.output.background.g, 200);
  EXPECT_EQ(plan.output.background.b, 100);
  EXPECT_EQ(plan.output.background.a, 255);
}

TEST(OlympusVsiPlanBuilderTest, RejectsInvalidLevelIndex) {
  const auto level = MakeLevel(0, 1, 1);
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};
  const auto req = MakeRegionRequest(/*level=*/5, 0, 0, 256, 256);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  EXPECT_FALSE(plan_or.ok());
}

TEST(OlympusVsiPlanBuilderTest, Uint16PixelTypeProducesUint16Plan) {
  const auto level = MakeLevel(0, 1, 1);
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};
  EtsHeader ets = MakeRgbEts(3);
  ets.pixel_type = TilePixelType::kUInt16;
  // 16-bit backgrounds; the plan-builder narrows them to 8 bits (top
  // byte) for the Canvas configuration step.
  ets.background = {0xFF00, 0x8000, 0x4000, 0};

  const auto req = MakeRegionRequest(0, 0, 0, 256, 256);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, ets);
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  const auto& plan = *plan_or;
  EXPECT_EQ(plan.output.pixel_format, core::OutputSpec::PixelFormat::kUInt16);
  EXPECT_EQ(plan.output.background.r, 0xFF);
  EXPECT_EQ(plan.output.background.g, 0x80);
  EXPECT_EQ(plan.output.background.b, 0x40);
}

TEST(OlympusVsiPlanBuilderTest, MultiChannelUint16ProducesSeparatePlanarPlan) {
  // Three stacked grayscale planes sharing one (x, y) grid: each plane
  // is keyed by PackKey3(channel, x, y). The plan builder must emit one
  // op per present plane, route each to its destination channel, and
  // configure a separate-planar spectral uint16 canvas.
  OlympusVsiLevelInfo level;
  level.level = 0;
  level.tile_w = 256;
  level.tile_h = 256;
  level.grid_cols = 1;
  level.grid_rows = 1;
  level.size = {256, 256};
  level.reported_size = {256, 256};
  level.downsample = 1.0;
  level.n_channels = 3;
  uint64_t off = 1024;
  for (uint32_t c = 0; c < 3; ++c) {
    level.tile_map[OlympusVsiLevelInfo::PackKey3(c, 0, 0)] =
        LevelTileEntry{off, 256};
    off += 256;
  }
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};

  EtsHeader ets = MakeRgbEts(1);
  ets.pixel_type = TilePixelType::kUInt16;

  const auto req = MakeRegionRequest(0, 0, 0, 256, 256);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, ets);
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  const auto& plan = *plan_or;

  EXPECT_EQ(plan.output.channels, 3U);
  EXPECT_EQ(plan.output.planar_config, PlanarConfig::kSeparate);
  EXPECT_TRUE(plan.output.force_spectral_image);
  EXPECT_EQ(plan.output.pixel_format, core::OutputSpec::PixelFormat::kUInt16);
  EXPECT_EQ(plan.operations.size(), 3U);

  std::set<uint32_t> dest_channels;
  for (const auto& op : plan.operations) {
    dest_channels.insert(op.channel_group_offset);
  }
  EXPECT_EQ(dest_channels.size(), 3U);
}

TEST(OlympusVsiPlanBuilderTest, Uint8PixelTypeProducesUint8Plan) {
  const auto level = MakeLevel(0, 1, 1);
  const std::vector<OlympusVsiLevelInfo> pyramid = {level};
  const auto req = MakeRegionRequest(0, 0, 0, 256, 256);
  auto plan_or = OlympusVsiPlanBuilder::BuildPlan(req, pyramid, MakeRgbEts());
  ASSERT_TRUE(plan_or.ok());
  EXPECT_EQ(plan_or->output.pixel_format,
            core::OutputSpec::PixelFormat::kUInt8);
}

}  // namespace
}  // namespace fastslide::formats::olympusvsi
