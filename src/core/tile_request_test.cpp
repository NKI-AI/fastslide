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

#include "fastslide/core/tile_request.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace fastslide {
namespace core {

// ============================================================================
// RegionSpec Tests
// ============================================================================

TEST(RegionSpecTest, ValidRegion) {
  RegionSpec region;
  region.top_left = ImageCoordinate{100, 200};
  region.size = ImageDimensions{512, 512};
  region.level = 0;

  EXPECT_TRUE(region.IsValid());
  EXPECT_EQ(region.top_left[0], 100);
  EXPECT_EQ(region.top_left[1], 200);
  EXPECT_EQ(region.size[0], 512);
  EXPECT_EQ(region.size[1], 512);
  EXPECT_EQ(region.level, 0);
}

TEST(RegionSpecTest, InvalidZeroWidth) {
  RegionSpec region;
  region.top_left = ImageCoordinate{0, 0};
  region.size = ImageDimensions{0, 512};
  region.level = 0;

  EXPECT_FALSE(region.IsValid());
}

TEST(RegionSpecTest, InvalidZeroHeight) {
  RegionSpec region;
  region.top_left = ImageCoordinate{0, 0};
  region.size = ImageDimensions{512, 0};
  region.level = 0;

  EXPECT_FALSE(region.IsValid());
}

TEST(RegionSpecTest, InvalidNegativeLevel) {
  RegionSpec region;
  region.top_left = ImageCoordinate{0, 0};
  region.size = ImageDimensions{512, 512};
  region.level = -1;

  EXPECT_FALSE(region.IsValid());
}

TEST(RegionSpecTest, MaxDimensions) {
  constexpr uint32_t max_val = std::numeric_limits<uint32_t>::max();

  RegionSpec region;
  region.top_left = ImageCoordinate{max_val - 1000, max_val - 1000};
  region.size = ImageDimensions{1000, 1000};
  region.level = 0;

  EXPECT_TRUE(region.IsValid());
}

TEST(RegionSpecTest, HighLevel) {
  RegionSpec region;
  region.top_left = ImageCoordinate{0, 0};
  region.size = ImageDimensions{128, 128};
  region.level = 10;  // High pyramid level

  EXPECT_TRUE(region.IsValid());
}

// ============================================================================
// TileCoordinate Tests
// ============================================================================

TEST(TileCoordinateTest, DefaultConstruction) {
  TileCoordinate coord{};
  EXPECT_EQ(coord.x, 0);
  EXPECT_EQ(coord.y, 0);
}

TEST(TileCoordinateTest, ValueConstruction) {
  TileCoordinate coord{42, 57};
  EXPECT_EQ(coord.x, 42);
  EXPECT_EQ(coord.y, 57);
}

TEST(TileCoordinateTest, Equality) {
  TileCoordinate a{10, 20};
  TileCoordinate b{10, 20};
  TileCoordinate c{10, 21};

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
}

TEST(TileCoordinateTest, MaxValues) {
  constexpr uint32_t max_val = std::numeric_limits<uint32_t>::max();
  TileCoordinate coord{max_val, max_val};

  EXPECT_EQ(coord.x, max_val);
  EXPECT_EQ(coord.y, max_val);
}

// ============================================================================
// FractionalRegionBounds Tests
// ============================================================================

TEST(FractionalRegionBoundsTest, ValidBounds) {
  FractionalRegionBounds bounds;
  bounds.x = 100.5;
  bounds.y = 200.75;
  bounds.width = 512.0;
  bounds.height = 512.0;

  EXPECT_TRUE(bounds.IsValid());
  EXPECT_DOUBLE_EQ(bounds.x, 100.5);
  EXPECT_DOUBLE_EQ(bounds.y, 200.75);
  EXPECT_DOUBLE_EQ(bounds.width, 512.0);
  EXPECT_DOUBLE_EQ(bounds.height, 512.0);
}

TEST(FractionalRegionBoundsTest, InvalidZeroWidth) {
  FractionalRegionBounds bounds;
  bounds.x = 0.0;
  bounds.y = 0.0;
  bounds.width = 0.0;
  bounds.height = 100.0;

  EXPECT_FALSE(bounds.IsValid());
}

TEST(FractionalRegionBoundsTest, InvalidZeroHeight) {
  FractionalRegionBounds bounds;
  bounds.x = 0.0;
  bounds.y = 0.0;
  bounds.width = 100.0;
  bounds.height = 0.0;

  EXPECT_FALSE(bounds.IsValid());
}

TEST(FractionalRegionBoundsTest, InvalidNegativeWidth) {
  FractionalRegionBounds bounds;
  bounds.x = 0.0;
  bounds.y = 0.0;
  bounds.width = -10.0;
  bounds.height = 100.0;

  EXPECT_FALSE(bounds.IsValid());
}

TEST(FractionalRegionBoundsTest, SubpixelPrecision) {
  FractionalRegionBounds bounds;
  bounds.x = 123.456789;
  bounds.y = 987.654321;
  bounds.width = 256.125;
  bounds.height = 256.875;

  EXPECT_TRUE(bounds.IsValid());
  EXPECT_NEAR(bounds.x, 123.456789, 1e-6);
  EXPECT_NEAR(bounds.y, 987.654321, 1e-6);
}

// ============================================================================
// TileRequest Tests
// ============================================================================

TEST(TileRequestTest, DefaultConstruction) {
  TileRequest request;

  EXPECT_EQ(request.level, 0);
  EXPECT_EQ(request.tile_coord.x, 0);
  EXPECT_EQ(request.tile_coord.y, 0);
  EXPECT_TRUE(request.channel_indices.empty());
  EXPECT_FALSE(request.region_bounds.has_value());
  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsAllChannels());
  EXPECT_FALSE(request.IsRegionRequest());
}

TEST(TileRequestTest, ConstructionWithLevelAndCoord) {
  TileRequest request(2, TileCoordinate{10, 20});

  EXPECT_EQ(request.level, 2);
  EXPECT_EQ(request.tile_coord.x, 10);
  EXPECT_EQ(request.tile_coord.y, 20);
  EXPECT_TRUE(request.channel_indices.empty());
  EXPECT_FALSE(request.region_bounds.has_value());
  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsAllChannels());
  EXPECT_FALSE(request.IsRegionRequest());
}

TEST(TileRequestTest, ConstructionWithChannels) {
  std::vector<size_t> channels = {0, 2, 4};
  TileRequest request(1, TileCoordinate{5, 7}, channels);

  EXPECT_EQ(request.level, 1);
  EXPECT_EQ(request.tile_coord.x, 5);
  EXPECT_EQ(request.tile_coord.y, 7);
  EXPECT_EQ(request.channel_indices.size(), 3);
  EXPECT_EQ(request.channel_indices[0], 0);
  EXPECT_EQ(request.channel_indices[1], 2);
  EXPECT_EQ(request.channel_indices[2], 4);
  EXPECT_FALSE(request.IsAllChannels());
  EXPECT_FALSE(request.IsRegionRequest());
}

TEST(TileRequestTest, ConstructionWithRegionBounds) {
  FractionalRegionBounds bounds{100.5, 200.25, 512.0, 256.0};
  TileRequest request(0, bounds);

  EXPECT_EQ(request.level, 0);
  EXPECT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->x, 100.5);
  EXPECT_DOUBLE_EQ(request.region_bounds->y, 200.25);
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 512.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 256.0);
  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsRegionRequest());
}

TEST(TileRequestTest, InvalidNegativeLevel) {
  TileRequest request;
  request.level = -1;

  EXPECT_FALSE(request.IsValid());
}

TEST(TileRequestTest, ChannelSelection) {
  TileRequest request;
  request.level = 0;
  request.tile_coord = {0, 0};

  // Initially all channels
  EXPECT_TRUE(request.IsAllChannels());

  // Add specific channels
  request.channel_indices = {1, 3, 5};
  EXPECT_FALSE(request.IsAllChannels());
  EXPECT_EQ(request.channel_indices.size(), 3);
}

TEST(TileRequestTest, RegionRequestWithChannels) {
  FractionalRegionBounds bounds{0.0, 0.0, 256.0, 256.0};
  TileRequest request(0, bounds);
  request.channel_indices = {0, 1, 2};

  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsRegionRequest());
  EXPECT_FALSE(request.IsAllChannels());
  EXPECT_EQ(request.channel_indices.size(), 3);
}

// ============================================================================
// MultiTileRequest Tests
// ============================================================================

TEST(MultiTileRequestTest, DefaultConstruction) {
  MultiTileRequest request;

  EXPECT_EQ(request.level, 0);
  EXPECT_TRUE(request.tile_coords.empty());
  EXPECT_TRUE(request.channel_indices.empty());
  EXPECT_FALSE(request.IsValid());  // No coordinates
  EXPECT_EQ(request.GetTileCount(), 0);
}

TEST(MultiTileRequestTest, ConstructionWithCoordinates) {
  std::vector<TileCoordinate> coords = {
      TileCoordinate{0, 0}, TileCoordinate{1, 0}, TileCoordinate{0, 1}};
  MultiTileRequest request(2, coords);

  EXPECT_EQ(request.level, 2);
  EXPECT_EQ(request.tile_coords.size(), 3);
  EXPECT_TRUE(request.channel_indices.empty());
  EXPECT_TRUE(request.IsValid());
  EXPECT_EQ(request.GetTileCount(), 3);
}

TEST(MultiTileRequestTest, InvalidNegativeLevel) {
  std::vector<TileCoordinate> coords = {TileCoordinate{0, 0}};
  MultiTileRequest request(-1, coords);

  EXPECT_FALSE(request.IsValid());
}

TEST(MultiTileRequestTest, InvalidEmptyCoordinates) {
  MultiTileRequest request;
  request.level = 0;
  request.tile_coords.clear();

  EXPECT_FALSE(request.IsValid());
}

TEST(MultiTileRequestTest, LargeNumberOfTiles) {
  std::vector<TileCoordinate> coords;
  for (uint32_t y = 0; y < 100; ++y) {
    for (uint32_t x = 0; x < 100; ++x) {
      coords.push_back(TileCoordinate{x, y});
    }
  }

  MultiTileRequest request(0, coords);

  EXPECT_TRUE(request.IsValid());
  EXPECT_EQ(request.GetTileCount(), 10000);
}

TEST(MultiTileRequestTest, WithChannelSelection) {
  std::vector<TileCoordinate> coords = {TileCoordinate{0, 0},
                                        TileCoordinate{1, 0}};
  MultiTileRequest request(1, coords);
  request.channel_indices = {0, 2};

  EXPECT_TRUE(request.IsValid());
  EXPECT_EQ(request.channel_indices.size(), 2);
  EXPECT_EQ(request.GetTileCount(), 2);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(TileRequestIntegrationTest, RegionSpecToRequestConversion) {
  // Simulate conversion from RegionSpec to TileRequest
  RegionSpec region;
  region.top_left = ImageCoordinate{1000, 2000};
  region.size = ImageDimensions{512, 256};
  region.level = 1;

  ASSERT_TRUE(region.IsValid());

  // Convert to TileRequest with fractional bounds
  FractionalRegionBounds bounds;
  bounds.x = static_cast<double>(region.top_left[0]);
  bounds.y = static_cast<double>(region.top_left[1]);
  bounds.width = static_cast<double>(region.size[0]);
  bounds.height = static_cast<double>(region.size[1]);

  TileRequest request(region.level, bounds);

  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsRegionRequest());
  EXPECT_EQ(request.level, region.level);
  EXPECT_DOUBLE_EQ(request.region_bounds->x,
                   static_cast<double>(region.top_left[0]));
  EXPECT_DOUBLE_EQ(request.region_bounds->y,
                   static_cast<double>(region.top_left[1]));
  EXPECT_DOUBLE_EQ(request.region_bounds->width,
                   static_cast<double>(region.size[0]));
  EXPECT_DOUBLE_EQ(request.region_bounds->height,
                   static_cast<double>(region.size[1]));
}

TEST(TileRequestIntegrationTest, MultiTileFromRegion) {
  // Test converting a region into multiple tile requests
  const uint32_t tile_size = 256;
  const uint32_t region_width = 1024;
  const uint32_t region_height = 768;

  // Calculate number of tiles needed
  const uint32_t tiles_x = (region_width + tile_size - 1) / tile_size;   // 4
  const uint32_t tiles_y = (region_height + tile_size - 1) / tile_size;  // 3

  std::vector<TileCoordinate> coords;
  for (uint32_t y = 0; y < tiles_y; ++y) {
    for (uint32_t x = 0; x < tiles_x; ++x) {
      coords.push_back(TileCoordinate{x, y});
    }
  }

  MultiTileRequest request(0, coords);

  EXPECT_TRUE(request.IsValid());
  EXPECT_EQ(request.GetTileCount(), 12);  // 4x3 = 12 tiles
}

}  // namespace core
}  // namespace fastslide
