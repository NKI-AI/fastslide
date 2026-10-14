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

#include "fastslide/core/tile_request.h"
#include "fastslide/readers/isyntax/isyntax_plan_context.h"
#include "gtest/gtest.h"

namespace fastslide {
namespace {

// `origin_offset_in_pixels` as libisyntax computes it for `level` (> 0).
int32_t OriginOffset(int32_t level) {
  return level == 0 ? 0 : (3 << (level - 1)) - 2;
}

IsyntaxPlanContext MakeContext(int32_t level, int64_t grid_offset,
                               int64_t grid_tiles) {
  IsyntaxPlanContext context;
  context.tile_size = {256, 256};
  const auto size = static_cast<uint32_t>(grid_tiles * 256);
  context.level_info = LevelInfo({size, size}, static_cast<double>(1 << level));
  context.grid_offset = grid_offset;
  context.grid_width_in_tiles = grid_tiles;
  context.grid_height_in_tiles = grid_tiles;
  return context;
}

core::TileRequest MakeRegionRequest(int level, double x, double y, double w,
                                    double h) {
  core::TileRequest req;
  req.level = level;
  req.region_bounds = core::FractionalRegionBounds{x, y, w, h};
  return req;
}

// 9-level slide: level0_padding = (3 << 9) - 3. The expected offsets are the
// ones for which every level is bit-exact with the Philips SDK.
TEST(IsyntaxPlanBuilderTest, GridOffsetMatchesPhilipsSdk) {
  constexpr int32_t kLevel0Padding = (3 << 9) - 3;
  constexpr int64_t kExpected[] = {1533, 766, 382, 190, 94, 46, 22, 10, 4};
  for (int32_t level = 0; level < 9; ++level) {
    EXPECT_EQ(IsyntaxPlanBuilder::ComputeGridOffset(level, kLevel0Padding,
                                                    OriginOffset(level)),
              kExpected[level])
        << "level " << level;
  }
}

TEST(IsyntaxPlanBuilderTest, TilesArePlacedOnWholePixels) {
  const auto context = MakeContext(/*level=*/3, /*grid_offset=*/190, 16);
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

TEST(IsyntaxPlanBuilderTest, TilesAreClampedToTheGrid) {
  // 4 x 4 tile grid; the region runs past its right and bottom edges.
  const auto context = MakeContext(/*level=*/0, /*grid_offset=*/10, 4);
  const auto req = MakeRegionRequest(0, 900, 900, 400, 400);
  auto plan_or = IsyntaxPlanBuilder::BuildPlan(req, context);
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
  ASSERT_EQ(plan_or->operations.size(), 1U);
  EXPECT_EQ(plan_or->operations.front().tile_coord.x, 3U);
  EXPECT_EQ(plan_or->operations.front().tile_coord.y, 3U);
}

}  // namespace
}  // namespace fastslide
