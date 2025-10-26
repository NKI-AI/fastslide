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

#include "fastslide/utilities/tiff/tile_utilities.h"

#include <algorithm>
#include <vector>

#include "gtest/gtest.h"

namespace fastslide {
namespace tiff {
namespace {

// Test fixture for tile utilities
class TileUtilitiesTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {}
};

TEST_F(TileUtilitiesTest, TileCoordinateIteratorBasic) {
  // Create a simple 2x2 tile grid (tiles are 100x100)
  uint32_t start_x = 0, start_y = 0;
  uint32_t end_x = 200, end_y = 200;
  uint32_t tile_width = 100, tile_height = 100;

  TileCoordinateIterator it(start_x, start_y, end_x, end_y, tile_width,
                            tile_height);
  TileCoordinateIterator end_it;

  std::vector<TileCoordinate> coords;
  for (; it != end_it; ++it) {
    coords.push_back(*it);
  }

  EXPECT_EQ(coords.size(), 4);
  EXPECT_EQ(coords[0], TileCoordinate({0, 0}));
  EXPECT_EQ(coords[1], TileCoordinate({100, 0}));
  EXPECT_EQ(coords[2], TileCoordinate({0, 100}));
  EXPECT_EQ(coords[3], TileCoordinate({100, 100}));
}

TEST_F(TileUtilitiesTest, TileCoordinateIteratorEmptyRange) {
  // Empty range should produce no coordinates
  TileCoordinateIterator it(100, 100, 100, 100, 50, 50);
  TileCoordinateIterator end_it;

  EXPECT_EQ(it, end_it);  // Should be equal to end immediately
}

TEST_F(TileUtilitiesTest, TileCoordinateRangeWithRegion) {
  // Test TileCoordinateRange with a RegionSpec
  RegionSpec region;
  region.top_left = {50, 75};  // Start in the middle of first tile
  region.size = {150, 125};    // Span multiple tiles

  aifocore::Size<uint32_t, 2> tile_dims{100, 100};
  TileCoordinateRange range(region, tile_dims);

  std::vector<TileCoordinate> coords;
  for (const auto& coord : range) {
    coords.push_back(coord);
  }

  // Should cover tiles at (0,0), (100,0), (0,100), (100,100)
  EXPECT_EQ(coords.size(), 4);
  EXPECT_TRUE(std::find(coords.begin(), coords.end(), TileCoordinate({0, 0})) !=
              coords.end());
  EXPECT_TRUE(std::find(coords.begin(), coords.end(),
                        TileCoordinate({100, 0})) != coords.end());
  EXPECT_TRUE(std::find(coords.begin(), coords.end(),
                        TileCoordinate({0, 100})) != coords.end());
  EXPECT_TRUE(std::find(coords.begin(), coords.end(),
                        TileCoordinate({100, 100})) != coords.end());
}

TEST_F(TileUtilitiesTest, CopyTileToBufferBasic) {
  // Create a simple 4x4 tile with known pattern
  std::vector<uint8_t> tile_buffer = {1, 2,  3,  4,  5,  6,  7,  8,
                                      9, 10, 11, 12, 13, 14, 15, 16};

  // Create output buffer for 2x2 region
  std::vector<uint8_t> output_buffer(4, 0);

  // Copy 2x2 region starting at (1,1) from tile at (0,0) to region (0,0) with size 2x2
  CopyTileToBuffer(tile_buffer.data(), output_buffer.data(), 4,
                   4,     // tile dimensions
                   0, 0,  // tile position
                   1, 1,  // region position
                   2, 2,  // region dimensions
                   1);    // bytes per pixel

  // Expected output should be the 2x2 subregion:
  // 6, 7
  // 10, 11
  std::vector<uint8_t> expected = {6, 7, 10, 11};
  EXPECT_EQ(output_buffer, expected);
}

TEST_F(TileUtilitiesTest, CopyTileToBufferNoIntersection) {
  // Test case where tile and region don't intersect
  std::vector<uint8_t> tile_buffer(16, 42);
  std::vector<uint8_t> output_buffer(4, 0);

  // Tile at (100, 100), region at (0, 0) - no intersection
  CopyTileToBuffer(tile_buffer.data(), output_buffer.data(), 4,
                   4,         // tile dimensions
                   100, 100,  // tile position
                   0, 0,      // region position
                   2, 2,      // region dimensions
                   1);        // bytes per pixel

  // Output buffer should remain unchanged (all zeros)
  std::vector<uint8_t> expected(4, 0);
  EXPECT_EQ(output_buffer, expected);
}

TEST_F(TileUtilitiesTest, CopyTileToBufferMultiBytePixels) {
  // Test with multi-byte pixels (RGB: 3 bytes per pixel)
  std::vector<uint8_t> tile_buffer = {
      255, 0, 0,   0,   255, 0,  // Red, Green
      0,   0, 255, 255, 255, 0   // Blue, Yellow
  };

  std::vector<uint8_t> output_buffer(6, 0);  // 2 pixels * 3 bytes

  // Copy 1x2 region (2 pixels)
  CopyTileToBuffer(tile_buffer.data(), output_buffer.data(), 2,
                   2,     // tile dimensions (2x2 pixels)
                   0, 0,  // tile position
                   0, 0,  // region position
                   2, 1,  // region dimensions (2x1 pixels)
                   3);    // bytes per pixel (RGB)

  // Should copy first row: Red + Green pixels
  std::vector<uint8_t> expected = {255, 0, 0, 0, 255, 0};
  EXPECT_EQ(output_buffer, expected);
}

}  // namespace
}  // namespace tiff
}  // namespace fastslide
