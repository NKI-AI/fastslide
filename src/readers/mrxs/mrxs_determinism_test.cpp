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

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/mrxs/mrxs.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {
namespace {

// Paints `plan` onto a fresh canvas and returns the pixels.
aifocore::Result<std::vector<uint8_t>> Paint(const MrxsReader& reader,
                                             const core::TilePlan& plan) {
  runtime::Canvas canvas(plan);
  AIFOCORE_RETURN_IF_ERROR(reader.ExecutePlan(plan, canvas));
  AIFOCORE_ASSIGN_OR_RETURN(auto image, canvas.GetOutput());
  return image.GetDataVector();
}

// Whether `a` shows over `b` where they overlap: OpenSlide draws the tile grid
// from the bottom-right and never paints over an opaque pixel.
bool ShowsOver(const core::TileReadOp& a, const core::TileReadOp& b) {
  if (a.tile_coord.y != b.tile_coord.y) {
    return a.tile_coord.y > b.tile_coord.y;
  }
  return a.tile_coord.x > b.tile_coord.x;
}

// Guarded end-to-end test. Set FASTSLIDE_MRXS_TEST_FILE to the path of a real
// MRXS slide. Skipped (not failed) when the variable is unset or the file is
// missing, so the suite stays hermetic in CI.
//
// MRXS camera tiles overlap; the same read must return the same pixels every
// time, whatever order the tiles finish decoding in.
TEST(MrxsDeterminismTest, RepeatedReadsReturnTheSamePixels) {
  const char* path = std::getenv("FASTSLIDE_MRXS_TEST_FILE");
  if (path == nullptr || !std::filesystem::exists(path)) {
    GTEST_SKIP()
        << "Set FASTSLIDE_MRXS_TEST_FILE to a .mrxs slide to run this.";
  }

  auto reader_or = MrxsReader::Create(path);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  auto& reader = *reader_or;

  const int level_count = reader->GetLevelCount();
  ASSERT_GT(level_count, 0);
  for (const int level : {0, level_count / 3, (2 * level_count) / 3}) {
    auto info_or = reader->GetLevelInfo(level);
    ASSERT_TRUE(info_or.ok());
    const auto dims = info_or->dimensions;
    // A region in the middle of the level, where camera tiles overlap.
    const uint32_t w = std::min<uint32_t>(512, dims[0]);
    const uint32_t h = std::min<uint32_t>(512, dims[1]);
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

// Where camera tiles overlap, the bottom-right tile shows, as in OpenSlide,
// whatever order the plan lists the operations in.
TEST(MrxsDeterminismTest, BottomRightTileWinsOverlaps) {
  const char* path = std::getenv("FASTSLIDE_MRXS_TEST_FILE");
  if (path == nullptr || !std::filesystem::exists(path)) {
    GTEST_SKIP()
        << "Set FASTSLIDE_MRXS_TEST_FILE to a .mrxs slide to run this.";
  }

  auto reader_or = MrxsReader::Create(path);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  const MrxsReader& reader = **reader_or;

  size_t overlap_pixels = 0;
  for (const int level : {0, reader.GetLevelCount() / 3}) {
    // Find two overlapping tiles, starting from the middle of the level.
    core::TileRequest whole_level;
    whole_level.level = level;
    auto all_or = reader.PrepareRequest(whole_level);
    ASSERT_TRUE(all_or.ok()) << all_or.status().message();
    const auto& all = all_or->operations;
    std::optional<std::pair<double, double>> overlap_center;
    const size_t last = std::min(all.size(), all.size() / 2 + 64);
    for (size_t i = all.size() / 2; i < last && !overlap_center; ++i) {
      const auto& a = all[i].transform.dest;
      for (size_t j = 0; j < all.size(); ++j) {
        const auto& b = all[j].transform.dest;
        const double x0 = std::max(a.x, b.x);
        const double x1 = std::min(a.x + a.width, b.x + b.width);
        const double y0 = std::max(a.y, b.y);
        const double y1 = std::min(a.y + a.height, b.y + b.height);
        if (j != i && x1 - x0 >= 2.0 && y1 - y0 >= 2.0) {
          overlap_center = {(x0 + x1) / 2, (y0 + y1) / 2};
          break;
        }
      }
    }
    if (!overlap_center) {
      continue;
    }

    constexpr uint32_t kSize = 256;
    core::TileRequest request;
    request.level = level;
    request.region_bounds = core::FractionalRegionBounds{
        std::max(0.0, std::floor(overlap_center->first) - kSize / 2),
        std::max(0.0, std::floor(overlap_center->second) - kSize / 2), kSize,
        kSize};
    auto plan_or = reader.PrepareRequest(request);
    ASSERT_TRUE(plan_or.ok()) << plan_or.status().message();
    const core::TilePlan& plan = *plan_or;
    if (plan.output.channels != 3) {
      GTEST_SKIP() << "Checks slides with RGB output.";
    }

    auto pixels_or = Paint(reader, plan);
    ASSERT_TRUE(pixels_or.ok()) << pixels_or.status().message();
    const std::vector<uint8_t>& pixels = *pixels_or;
    const size_t pixel_bytes = pixels.size() / (size_t{kSize} * kSize);

    core::TilePlan reversed = plan;
    std::reverse(reversed.operations.begin(), reversed.operations.end());
    auto reversed_or = Paint(reader, reversed);
    ASSERT_TRUE(reversed_or.ok()) << reversed_or.status().message();
    EXPECT_EQ(*reversed_or, pixels) << "level " << level;

    // Each pixel shows the tile that wins it, as that tile paints it alone.
    std::vector<int> shown(size_t{kSize} * kSize, -1);
    for (int i = 0; i < static_cast<int>(plan.operations.size()); ++i) {
      const auto& op = plan.operations[i];
      const auto& dest = op.transform.dest;
      const int x0 = std::max(0, static_cast<int>(std::floor(dest.x)));
      const int x1 = std::min(static_cast<int>(kSize),
                              static_cast<int>(std::ceil(dest.x + dest.width)));
      const int y0 = std::max(0, static_cast<int>(std::floor(dest.y)));
      const int y1 =
          std::min(static_cast<int>(kSize),
                   static_cast<int>(std::ceil(dest.y + dest.height)));
      for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
          int& winner = shown[static_cast<size_t>(y) * kSize + x];
          if (winner >= 0) {
            ++overlap_pixels;
          }
          if (winner < 0 || ShowsOver(op, plan.operations[winner])) {
            winner = i;
          }
        }
      }
    }
    for (int i = 0; i < static_cast<int>(plan.operations.size()); ++i) {
      core::TilePlan alone = plan;
      alone.operations = {plan.operations[i]};
      auto alone_or = Paint(reader, alone);
      ASSERT_TRUE(alone_or.ok()) << alone_or.status().message();
      size_t mismatches = 0;
      for (size_t p = 0; p < shown.size(); ++p) {
        if (shown[p] == i && !std::equal(pixels.begin() + p * pixel_bytes,
                                         pixels.begin() + (p + 1) * pixel_bytes,
                                         alone_or->begin() + p * pixel_bytes)) {
          ++mismatches;
        }
      }
      EXPECT_EQ(mismatches, 0U)
          << "level " << level << ", tile (" << plan.operations[i].tile_coord.x
          << ", " << plan.operations[i].tile_coord.y << ")";
    }
  }
  if (overlap_pixels == 0) {
    GTEST_SKIP() << "Found no overlapping tiles to check.";
  }
}

}  // namespace
}  // namespace fastslide
