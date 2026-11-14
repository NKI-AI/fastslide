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

#include "fastslide/readers/isyntax/isyntax_plan_builder.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "fastslide/core/tile_request.h"
#include "fastslide/readers/isyntax/isyntax_plan_context.h"
#include "gtest/gtest.h"

namespace fastslide {
namespace {

// 9-level slide.
constexpr int32_t kLevel0Padding = (3 << 9) - 3;

// `origin_offset_in_pixels` as libisyntax computes it for `level` (> 0).
int32_t OriginOffset(int32_t level) {
  return level == 0 ? 0 : (3 << (level - 1)) - 2;
}

IsyntaxPlanContext MakeContext(int32_t level, int32_t level0_padding,
                               int64_t grid_tiles, bool subpixel_correction) {
  IsyntaxPlanContext context;
  context.tile_size = {256, 256};
  const auto size = static_cast<uint32_t>(grid_tiles * 256);
  context.level_info = LevelInfo({size, size}, static_cast<double>(1 << level));
  context.level0_padding = level0_padding;
  context.origin_offset_in_pixels = OriginOffset(level);
  context.grid_width_in_tiles = grid_tiles;
  context.grid_height_in_tiles = grid_tiles;
  context.subpixel_correction = subpixel_correction;
  return context;
}

core::TileRequest MakeRegionRequest(int level, double x, double y, double w,
                                    double h) {
  core::TileRequest req;
  req.level = level;
  req.region_bounds = core::FractionalRegionBounds{x, y, w, h};
  return req;
}

// The expected offsets are the ones for which every level is bit-exact with
// the Philips SDK.
TEST(IsyntaxPlanBuilderTest, GridOffsetMatchesPhilipsSdk) {
  constexpr int64_t kExpected[] = {1533, 766, 382, 190, 94, 46, 22, 10, 4};
  for (int32_t level = 0; level < 9; ++level) {
    EXPECT_EQ(IsyntaxPlanBuilder::ComputeGridOffset(level, kLevel0Padding,
                                                    OriginOffset(level)),
              kExpected[level])
        << "level " << level;
  }
}

TEST(IsyntaxPlanBuilderTest, OriginShiftIsZeroAtLevel0) {
  EXPECT_EQ(IsyntaxPlanBuilder::ComputeOriginShift(0, 1.0), 0.0);
  EXPECT_DOUBLE_EQ(IsyntaxPlanBuilder::ComputeOriginShift(1, 2.0), -0.75);
  EXPECT_DOUBLE_EQ(IsyntaxPlanBuilder::ComputeOriginShift(3, 8.0), -1.3125);
}

// The corrected offset equals the Philips SDK one at level 0 and differs from
// it by 0.5 - 1.5 / downsample level pixels above it.
TEST(IsyntaxPlanBuilderTest, CorrectedGridOffsetIsSdkPlusSubpixelShift) {
  for (int32_t level = 0; level < 9; ++level) {
    const double downsample = static_cast<double>(1 << level);
    const double expected_delta = level == 0 ? 0.0 : 0.5 - (1.5 / downsample);
    const double sdk =
        static_cast<double>(IsyntaxPlanBuilder::ComputeGridOffset(
            level, kLevel0Padding, OriginOffset(level)));
    EXPECT_DOUBLE_EQ(IsyntaxPlanBuilder::ComputeCorrectedGridOffset(
                         level, kLevel0Padding, downsample) -
                         sdk,
                     expected_delta)
        << "level " << level;
  }
}

// `DimensionRanges` of the Philips SDK 5.1 for every level, in level 0
// pixels: start (both axes), end x, end y.
struct SdkRange {
  int64_t start;
  int64_t end_x;
  int64_t end_y;
};

void ExpectSdkRanges(int64_t width, int64_t height,
                     const std::vector<SdkRange>& sdk) {
  for (int32_t level = 0; level < static_cast<int32_t>(sdk.size()); ++level) {
    const auto range =
        IsyntaxPlanBuilder::ComputeValidRange(level, width, height);
    EXPECT_EQ(range.start, sdk[level].start >> level) << "level " << level;
    EXPECT_EQ(range.end_x, sdk[level].end_x >> level) << "level " << level;
    EXPECT_EQ(range.end_y, sdk[level].end_y >> level) << "level " << level;
  }
}

TEST(IsyntaxPlanBuilderTest, ValidRangeMatchesPhilipsSdkOnIsyntaxV1) {
  // 158726 x 91142, 10 levels.
  ExpectSdkRanges(158726, 91142,
                  {{0, 158725, 91141},
                   {2, 158722, 91138},
                   {8, 158716, 91132},
                   {16, 158704, 91120},
                   {32, 158688, 91104},
                   {64, 158656, 91072},
                   {128, 158592, 91008},
                   {256, 158464, 90880},
                   {512, 158208, 90624},
                   {1024, 157696, 90112}});
}

TEST(IsyntaxPlanBuilderTest, ValidRangeMatchesPhilipsSdkOnNineLevels) {
  // 37382 x 73222, 9 levels.
  ExpectSdkRanges(37382, 73222,
                  {{0, 37381, 73221},
                   {2, 37378, 73218},
                   {8, 37372, 73212},
                   {16, 37360, 73200},
                   {32, 37344, 73184},
                   {64, 37312, 73152},
                   {128, 37248, 73088},
                   {256, 37120, 72960},
                   {512, 36864, 72704}});
}

TEST(IsyntaxPlanBuilderTest, ValidRangeMatchesPhilipsSdkOnOddI2syntax) {
  // 171998 x 71111 (odd height), iSyntax v2, 10 levels.
  ExpectSdkRanges(171998, 71111,
                  {{0, 171997, 71110},
                   {2, 171994, 71108},
                   {8, 171988, 71104},
                   {16, 171976, 71096},
                   {32, 171952, 71072},
                   {64, 171904, 71040},
                   {128, 171840, 70976},
                   {256, 171648, 70784},
                   {512, 171264, 70400},
                   {1024, 170496, 69632}});
}

TEST(IsyntaxPlanBuilderTest, TilesArePlacedOnWholePixels) {
  // Without the correction, level 3 has the SDK offset 190.
  const auto context = MakeContext(/*level=*/3, kLevel0Padding, 16,
                                   /*subpixel_correction=*/false);
  const auto req = MakeRegionRequest(3, 100, 50, 300, 300);
  auto plan_or = IsyntaxPlanBuilder::BuildPlan(req, context);
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  const auto& plan = *plan_or;

  // Grid x range [290, 590) and y range [240, 540) span 2 x 3 tiles.
  ASSERT_EQ(plan.operations.size(), 6U);
  for (const auto& op : plan.operations) {
    EXPECT_EQ(op.transform.source.x, std::floor(op.transform.source.x));
    EXPECT_EQ(op.transform.source.y, std::floor(op.transform.source.y));
    EXPECT_EQ(op.transform.dest.x, std::floor(op.transform.dest.x));
    EXPECT_EQ(op.transform.dest.y, std::floor(op.transform.dest.y));
  }
  const auto& first = plan.operations.front();
  EXPECT_EQ(first.tile_coord.x, 1U);
  EXPECT_EQ(first.tile_coord.y, 0U);
  EXPECT_EQ(first.transform.source.x, 290.0 - 256.0);
  EXPECT_EQ(first.transform.source.y, 240.0);
  EXPECT_EQ(first.transform.dest.x, 0.0);
  EXPECT_EQ(first.transform.dest.y, 0.0);
}

TEST(IsyntaxPlanBuilderTest, SubpixelCorrectionShiftsHigherLevels) {
  // Level 3: offset = 1533 / 8 - 1.5 + 1.5 / 8 = 190.3125.
  const auto context = MakeContext(/*level=*/3, kLevel0Padding, 16,
                                   /*subpixel_correction=*/true);
  const auto req = MakeRegionRequest(3, 100, 50, 300, 300);
  auto plan_or = IsyntaxPlanBuilder::BuildPlan(req, context);
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  const auto& plan = *plan_or;

  // Grid x range [290.3125, 590.3125) and y range [240.3125, 540.3125) span
  // 2 x 3 tiles. Sources stay on whole tile pixels (the Canvas would truncate
  // a fractional one); the fraction is in the destination.
  ASSERT_EQ(plan.operations.size(), 6U);
  for (const auto& op : plan.operations) {
    EXPECT_EQ(op.transform.source.x, std::floor(op.transform.source.x));
    EXPECT_EQ(op.transform.source.y, std::floor(op.transform.source.y));
    EXPECT_DOUBLE_EQ(op.transform.dest.x - std::floor(op.transform.dest.x),
                     0.6875);
    EXPECT_DOUBLE_EQ(op.transform.dest.y - std::floor(op.transform.dest.y),
                     0.6875);
  }
  // Grid pixel 290.3125 is tile 1's pixel 34.3125: the source starts at its
  // pixel 34, placed 0.3125 pixels left of the region.
  const auto& first = plan.operations.front();
  EXPECT_EQ(first.tile_coord.x, 1U);
  EXPECT_EQ(first.tile_coord.y, 0U);
  EXPECT_EQ(first.transform.source.x, 34.0);
  EXPECT_EQ(first.transform.source.y, 240.0);
  EXPECT_EQ(first.transform.source.width, 256U - 34U);
  EXPECT_EQ(first.transform.source.height, 256U - 240U);
  EXPECT_DOUBLE_EQ(first.transform.dest.x, -0.3125);
  EXPECT_DOUBLE_EQ(first.transform.dest.y, -0.3125);
}

TEST(IsyntaxPlanBuilderTest, SubpixelCorrectionLeavesLevel0Unchanged) {
  const auto req = MakeRegionRequest(0, 100, 50, 300, 300);
  auto off_or = IsyntaxPlanBuilder::BuildPlan(
      req, MakeContext(0, kLevel0Padding, 16, /*subpixel_correction=*/false));
  auto on_or = IsyntaxPlanBuilder::BuildPlan(
      req, MakeContext(0, kLevel0Padding, 16, /*subpixel_correction=*/true));
  ASSERT_TRUE(off_or.ok() && on_or.ok());
  ASSERT_EQ(off_or->operations.size(), on_or->operations.size());
  for (size_t i = 0; i < off_or->operations.size(); ++i) {
    const auto& off = off_or->operations[i].transform;
    const auto& on = on_or->operations[i].transform;
    EXPECT_EQ(off_or->operations[i].tile_coord.x,
              on_or->operations[i].tile_coord.x);
    EXPECT_EQ(off_or->operations[i].tile_coord.y,
              on_or->operations[i].tile_coord.y);
    EXPECT_EQ(off.source.x, on.source.x);
    EXPECT_EQ(off.source.y, on.source.y);
    EXPECT_EQ(off.source.width, on.source.width);
    EXPECT_EQ(off.source.height, on.source.height);
    EXPECT_EQ(off.dest.x, on.dest.x);
    EXPECT_EQ(off.dest.y, on.dest.y);
  }
}

TEST(IsyntaxPlanBuilderTest, TilesAreClampedToTheGrid) {
  // 4 x 4 tile grid at offset 10; the region runs past its right and bottom
  // edges.
  const auto context = MakeContext(/*level=*/0, /*level0_padding=*/10, 4,
                                   /*subpixel_correction=*/false);
  const auto req = MakeRegionRequest(0, 900, 900, 400, 400);
  auto plan_or = IsyntaxPlanBuilder::BuildPlan(req, context);
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  ASSERT_EQ(plan_or->operations.size(), 1U);
  EXPECT_EQ(plan_or->operations.front().tile_coord.x, 3U);
  EXPECT_EQ(plan_or->operations.front().tile_coord.y, 3U);
}

}  // namespace
}  // namespace fastslide
