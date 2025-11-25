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

#include "fastslide/slide_reader.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace fastslide {

/// @brief Mock reader for testing RegionToTileRequest conversion
class MockReaderForConversion : public SlideReader {
 public:
  explicit MockReaderForConversion(int num_levels = 3)
      : num_levels_(num_levels) {}

  [[nodiscard]] int GetLevelCount() const override { return num_levels_; }

  [[nodiscard]] absl::StatusOr<LevelInfo> GetLevelInfo(
      int level) const override {
    if (level < 0 || level >= num_levels_) {
      return absl::InvalidArgumentError("Invalid level");
    }

    LevelInfo info;
    info.dimensions = {static_cast<uint32_t>(4096 >> level),
                       static_cast<uint32_t>(4096 >> level)};
    info.downsample_factor = static_cast<double>(1 << level);
    return info;
  }

  [[nodiscard]] const SlideProperties& GetProperties() const override {
    static SlideProperties props;
    return props;
  }

  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override {
    return {};
  }

  [[nodiscard]] absl::StatusOr<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override {
    return absl::NotFoundError("Not found");
  }

  [[nodiscard]] absl::StatusOr<Image> ReadAssociatedImage(
      std::string_view name) const override {
    return absl::NotFoundError("Not found");
  }

  [[nodiscard]] Metadata GetMetadata() const override { return Metadata(); }

  [[nodiscard]] std::string GetFormatName() const override {
    return "MockFormat";
  }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override {
    return ImageDimensions{256, 256};
  }

  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override {
    std::vector<ChannelMetadata> channels;
    channels.emplace_back("RGB", "RGB", ColorRGB(255, 255, 255), 0, 8);
    return channels;
  }

  // Expose protected method for testing
  absl::StatusOr<core::TileRequest> TestRegionToTileRequest(
      const RegionSpec& region) const {
    return RegionToTileRequest(region);
  }

 private:
  int num_levels_;
};

// ============================================================================
// RegionToTileRequest Conversion Tests
// ============================================================================

TEST(RegionConversionTest, BasicConversion) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {100, 200};
  region.size = {512, 512};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_EQ(request.level, 0);
  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsRegionRequest());

  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->x, 100.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->y, 200.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 512.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 512.0);
}

TEST(RegionConversionTest, InvalidRegionZeroWidth) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {0, 512};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RegionConversionTest, InvalidRegionZeroHeight) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {512, 0};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RegionConversionTest, InvalidLevelNegative) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {512, 512};
  region.level = -1;

  auto result = reader.TestRegionToTileRequest(region);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RegionConversionTest, InvalidLevelOutOfRange) {
  MockReaderForConversion reader(3);  // Only 3 levels (0, 1, 2)

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {512, 512};
  region.level = 5;  // Out of range

  auto result = reader.TestRegionToTileRequest(region);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RegionConversionTest, HigherPyramidLevel) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {50, 100};
  region.size = {256, 256};
  region.level = 2;  // Level 2 (downsampled)

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_EQ(request.level, 2);
  EXPECT_TRUE(request.IsRegionRequest());

  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->x, 50.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->y, 100.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 256.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 256.0);
}

TEST(RegionConversionTest, LargeRegion) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {4096, 4096};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_EQ(request.level, 0);
  EXPECT_TRUE(request.IsRegionRequest());

  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 4096.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 4096.0);
}

TEST(RegionConversionTest, SmallRegion) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {1000, 1000};
  region.size = {1, 1};  // Single pixel
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsRegionRequest());

  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 1.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 1.0);
}

TEST(RegionConversionTest, MaxCoordinates) {
  MockReaderForConversion reader;

  const uint32_t large_coord = std::numeric_limits<uint32_t>::max() - 1000;

  RegionSpec region;
  region.top_left = {large_coord, large_coord};
  region.size = {100, 100};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_TRUE(request.IsValid());
  EXPECT_TRUE(request.IsRegionRequest());

  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->x, static_cast<double>(large_coord));
  EXPECT_DOUBLE_EQ(request.region_bounds->y, static_cast<double>(large_coord));
}

// ============================================================================
// Channel Selection Tests
// ============================================================================

TEST(RegionConversionTest, NoChannelSelection) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {256, 256};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_TRUE(request.channel_indices.empty());
  EXPECT_TRUE(request.IsAllChannels());
}

TEST(RegionConversionTest, WithChannelSelection) {
  MockReaderForConversion reader;

  // Set visible channels
  reader.SetVisibleChannels({0, 2, 4});

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {256, 256};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_FALSE(request.channel_indices.empty());
  EXPECT_FALSE(request.IsAllChannels());
  EXPECT_EQ(request.channel_indices.size(), 3);
  EXPECT_EQ(request.channel_indices[0], 0);
  EXPECT_EQ(request.channel_indices[1], 2);
  EXPECT_EQ(request.channel_indices[2], 4);
}

TEST(RegionConversionTest, SingleChannelSelection) {
  MockReaderForConversion reader;

  reader.SetVisibleChannels({1});

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {256, 256};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_EQ(request.channel_indices.size(), 1);
  EXPECT_EQ(request.channel_indices[0], 1);
}

TEST(RegionConversionTest, ResetChannelSelection) {
  MockReaderForConversion reader;

  // Set channels, then reset
  reader.SetVisibleChannels({0, 1, 2});
  reader.ShowAllChannels();

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {256, 256};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  EXPECT_TRUE(request.channel_indices.empty());
  EXPECT_TRUE(request.IsAllChannels());
}

// ============================================================================
// Coordinate Precision Tests
// ============================================================================

TEST(RegionConversionTest, PreservesUInt32Precision) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {1234567, 7654321};
  region.size = {987654, 456789};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  ASSERT_TRUE(request.region_bounds.has_value());

  // Check that uint32 coordinates are accurately represented as doubles
  EXPECT_DOUBLE_EQ(request.region_bounds->x, 1234567.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->y, 7654321.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 987654.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 456789.0);

  // Verify no precision loss (double can represent uint32 exactly)
  EXPECT_EQ(static_cast<uint32_t>(request.region_bounds->x), 1234567);
  EXPECT_EQ(static_cast<uint32_t>(request.region_bounds->y), 7654321);
  EXPECT_EQ(static_cast<uint32_t>(request.region_bounds->width), 987654);
  EXPECT_EQ(static_cast<uint32_t>(request.region_bounds->height), 456789);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(RegionConversionIntegrationTest, ConversionWithPriorValidation) {
  MockReaderForConversion reader;

  // Test conversion of a valid region (clamping is tested separately in slide_reader_test.cpp)
  RegionSpec region;
  region.top_left = {100, 200};
  region.size = {512, 512};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->x, 100.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->y, 200.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 512.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 512.0);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(RegionConversionTest, OriginRegion) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {256, 256};
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->x, 0.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->y, 0.0);
}

TEST(RegionConversionTest, NonSquareRegion) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {100, 200};
  region.size = {1024, 256};  // Wide rectangle
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 1024.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 256.0);
}

TEST(RegionConversionTest, TallRectangle) {
  MockReaderForConversion reader;

  RegionSpec region;
  region.top_left = {100, 200};
  region.size = {256, 2048};  // Tall rectangle
  region.level = 0;

  auto result = reader.TestRegionToTileRequest(region);
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& request = result.value();
  ASSERT_TRUE(request.region_bounds.has_value());
  EXPECT_DOUBLE_EQ(request.region_bounds->width, 256.0);
  EXPECT_DOUBLE_EQ(request.region_bounds->height, 2048.0);
}

}  // namespace fastslide
