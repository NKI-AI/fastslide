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

#include "fastslide/readers/isyntax/isyntax_envelopes.h"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <vector>

#include "gtest/gtest.h"

namespace fastslide {
namespace {

using Rect = std::tuple<int64_t, int64_t, int64_t, int64_t>;  // x, y, w, h

std::vector<Rect> Sorted(const std::vector<SlideBounds>& regions) {
  std::vector<Rect> rects;
  for (const auto& r : regions) {
    rects.emplace_back(r.x, r.y, r.width, r.height);
  }
  std::sort(rects.begin(), rects.end());
  return rects;
}

// First envelope of an i2syntax header (level0_padding 1533), padded grid
// pixels, in the order of the header.
IsyntaxEnvelopes::Polygon I2syntaxEnvelope() {
  return {{1535, 51168},  {1535, 66142},  {5535, 21041},  {5535, 51168},
          {5535, 66142},  {5535, 72211},  {9535, 11938},  {9535, 21041},
          {9535, 72211},  {9535, 72644},  {17531, 11938}, {17531, 12372},
          {17531, 69393}, {17531, 72644}, {21531, 12372}, {21531, 15406},
          {21531, 64842}, {21531, 69393}, {25531, 15406}, {25531, 22342},
          {25531, 60074}, {25531, 64842}, {29531, 22342}, {29531, 60074}};
}

// Rectangles of that envelope reported by the Philips SDK 5.1
// (`envelopes_as_rectangles`), in pixels of each level.
TEST(IsyntaxEnvelopesTest, RegionsMatchPhilipsSdkOnI2syntax) {
  const auto envelopes =
      IsyntaxEnvelopes::FromHeader({I2syntaxEnvelope()}, 1533);
  EXPECT_EQ(Sorted(envelopes.Regions(0)),
            (std::vector<Rect>{{2, 49635, 4000, 14974},
                               {4002, 19508, 4000, 51170},
                               {8002, 10405, 7996, 60706},
                               {15998, 10839, 4000, 57021},
                               {19998, 13873, 4000, 49436},
                               {23998, 20809, 4000, 37732}}));
  EXPECT_EQ(Sorted(envelopes.Regions(1)),
            (std::vector<Rect>{{2, 24819, 2000, 7485},
                               {2002, 9755, 2000, 25583},
                               {4002, 5204, 3996, 30351},
                               {7998, 5421, 2000, 28508},
                               {9998, 6938, 2000, 24716},
                               {11998, 10406, 2000, 18864}}));
  EXPECT_EQ(Sorted(envelopes.Regions(3)),
            (std::vector<Rect>{{2, 6207, 500, 1868},
                               {502, 2441, 500, 6392},
                               {1002, 1303, 996, 7585},
                               {1998, 1357, 500, 7124},
                               {2498, 1736, 500, 6176},
                               {2998, 2603, 500, 4713}}));
  EXPECT_EQ(Sorted(envelopes.Regions(8)),
            (std::vector<Rect>{{2, 196, 16, 55},
                               {18, 79, 16, 196},
                               {34, 43, 27, 233},
                               {61, 45, 16, 219},
                               {77, 57, 15, 189},
                               {92, 84, 16, 143}}));
}

TEST(IsyntaxEnvelopesTest, BoundsOfHeaderEnvelopes) {
  const auto envelopes =
      IsyntaxEnvelopes::FromHeader({I2syntaxEnvelope()}, 1533);
  const auto bounds = envelopes.Bounds(171998, 71111);
  EXPECT_EQ(bounds.x, 2);
  EXPECT_EQ(bounds.y, 10405);
  EXPECT_EQ(bounds.width, 27996);
  EXPECT_EQ(bounds.height, 60706);
}

// Two groups of tiles: the box around each, moved to the API origin and
// clipped to the image, then shrunk at every level like the level range.
TEST(IsyntaxEnvelopesTest, TileGroupsOfAnIsyntaxV1Image) {
  // 6 x 3 tiles of 256 px, padding 300, image 1000 x 400.
  const std::vector<uint8_t> present = {
      1, 1, 0, 0, 0, 0,  //
      0, 1, 0, 0, 1, 1,  //
      0, 0, 0, 0, 1, 0,  //
  };
  const auto envelopes =
      IsyntaxEnvelopes::FromTiles(present, 6, 256, 256, 300, 1000, 400);
  EXPECT_EQ(Sorted(envelopes.Regions(0)),
            (std::vector<Rect>{{0, 0, 212, 212}, {724, 0, 276, 400}}));
  EXPECT_EQ(Sorted(envelopes.Regions(1)),
            (std::vector<Rect>{{1, 1, 104, 104}, {363, 1, 136, 198}}));
  const auto bounds = envelopes.Bounds(1000, 400);
  EXPECT_EQ(bounds.x, 0);
  EXPECT_EQ(bounds.y, 0);
  EXPECT_EQ(bounds.width, 1000);
  EXPECT_EQ(bounds.height, 400);
}

TEST(IsyntaxEnvelopesTest, NoEnvelopeHasNoBounds) {
  const std::vector<uint8_t> present = {0, 0, 0, 0};
  const auto envelopes =
      IsyntaxEnvelopes::FromTiles(present, 2, 256, 256, 300, 500, 500);
  EXPECT_TRUE(envelopes.empty());
  EXPECT_FALSE(envelopes.Bounds(500, 500).IsValid());
  EXPECT_TRUE(envelopes.Regions(0).empty());
}

}  // namespace
}  // namespace fastslide
