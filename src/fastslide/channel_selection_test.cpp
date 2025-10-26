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

/// @brief Mock reader for testing channel selection
class MockReaderForChannelSelection : public SlideReader {
 public:
  MockReaderForChannelSelection() = default;

  [[nodiscard]] int GetLevelCount() const override { return 1; }

  [[nodiscard]] absl::StatusOr<LevelInfo> GetLevelInfo(
      int level) const override {
    if (level != 0) {
      return absl::InvalidArgumentError("Invalid level");
    }
    LevelInfo info;
    info.dimensions = {1024, 1024};
    info.downsample_factor = 1.0;
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
};

// ============================================================================
// Basic Channel Selection Tests
// ============================================================================

TEST(ChannelSelectionTest, DefaultAllChannelsVisible) {
  MockReaderForChannelSelection reader;

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_TRUE(channels.empty());  // Empty means all channels
}

TEST(ChannelSelectionTest, SetSingleChannel) {
  MockReaderForChannelSelection reader;

  reader.SetVisibleChannels({0});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 1);
  EXPECT_EQ(channels[0], 0);
}

TEST(ChannelSelectionTest, SetMultipleChannels) {
  MockReaderForChannelSelection reader;

  reader.SetVisibleChannels({0, 2, 4});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 3);
  EXPECT_EQ(channels[0], 0);
  EXPECT_EQ(channels[1], 2);
  EXPECT_EQ(channels[2], 4);
}

TEST(ChannelSelectionTest, SetRGBChannels) {
  MockReaderForChannelSelection reader;

  reader.SetVisibleChannels({0, 1, 2});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 3);
  EXPECT_EQ(channels[0], 0);
  EXPECT_EQ(channels[1], 1);
  EXPECT_EQ(channels[2], 2);
}

TEST(ChannelSelectionTest, ResetToAllChannels) {
  MockReaderForChannelSelection reader;

  // Set specific channels
  reader.SetVisibleChannels({1, 3, 5});
  EXPECT_EQ(reader.GetVisibleChannels().size(), 3);

  // Reset to all channels
  reader.ShowAllChannels();
  EXPECT_TRUE(reader.GetVisibleChannels().empty());
}

TEST(ChannelSelectionTest, UpdateChannelSelection) {
  MockReaderForChannelSelection reader;

  // Initial selection
  reader.SetVisibleChannels({0, 1});
  EXPECT_EQ(reader.GetVisibleChannels().size(), 2);

  // Update to different channels
  reader.SetVisibleChannels({2, 3, 4});
  EXPECT_EQ(reader.GetVisibleChannels().size(), 3);
  EXPECT_EQ(reader.GetVisibleChannels()[0], 2);
  EXPECT_EQ(reader.GetVisibleChannels()[1], 3);
  EXPECT_EQ(reader.GetVisibleChannels()[2], 4);
}

TEST(ChannelSelectionTest, SetEmptyChannels) {
  MockReaderForChannelSelection reader;

  // Set some channels first
  reader.SetVisibleChannels({0, 1, 2});
  EXPECT_EQ(reader.GetVisibleChannels().size(), 3);

  // Set empty (equivalent to ShowAllChannels)
  reader.SetVisibleChannels({});
  EXPECT_TRUE(reader.GetVisibleChannels().empty());
}

// ============================================================================
// Channel Index Validation Tests
// ============================================================================

TEST(ChannelSelectionTest, LargeChannelIndices) {
  MockReaderForChannelSelection reader;

  // Test with large (but valid) channel indices
  reader.SetVisibleChannels({100, 200, 300});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 3);
  EXPECT_EQ(channels[0], 100);
  EXPECT_EQ(channels[1], 200);
  EXPECT_EQ(channels[2], 300);
}

TEST(ChannelSelectionTest, NonSequentialChannels) {
  MockReaderForChannelSelection reader;

  reader.SetVisibleChannels({5, 2, 8, 1});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 4);
  // Should preserve order as provided
  EXPECT_EQ(channels[0], 5);
  EXPECT_EQ(channels[1], 2);
  EXPECT_EQ(channels[2], 8);
  EXPECT_EQ(channels[3], 1);
}

TEST(ChannelSelectionTest, DuplicateChannels) {
  MockReaderForChannelSelection reader;

  // Set with duplicates
  reader.SetVisibleChannels({0, 0, 1, 1, 2});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 5);
  // Should preserve duplicates as provided (up to implementation)
}

TEST(ChannelSelectionTest, ManyChannels) {
  MockReaderForChannelSelection reader;

  // Test with many channels (e.g., hyperspectral imaging)
  std::vector<size_t> many_channels;
  for (size_t i = 0; i < 50; ++i) {
    many_channels.push_back(i);
  }

  reader.SetVisibleChannels(many_channels);

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 50);
  EXPECT_EQ(channels[0], 0);
  EXPECT_EQ(channels[49], 49);
}

// ============================================================================
// State Management Tests
// ============================================================================

TEST(ChannelSelectionTest, MultipleResets) {
  MockReaderForChannelSelection reader;

  // Set, reset, set, reset multiple times
  reader.SetVisibleChannels({0, 1, 2});
  EXPECT_EQ(reader.GetVisibleChannels().size(), 3);

  reader.ShowAllChannels();
  EXPECT_TRUE(reader.GetVisibleChannels().empty());

  reader.SetVisibleChannels({3, 4});
  EXPECT_EQ(reader.GetVisibleChannels().size(), 2);

  reader.ShowAllChannels();
  EXPECT_TRUE(reader.GetVisibleChannels().empty());
}

TEST(ChannelSelectionTest, StateIndependence) {
  MockReaderForChannelSelection reader1;
  MockReaderForChannelSelection reader2;

  // Set different channels on each reader
  reader1.SetVisibleChannels({0, 1});
  reader2.SetVisibleChannels({2, 3, 4});

  // Verify they are independent
  EXPECT_EQ(reader1.GetVisibleChannels().size(), 2);
  EXPECT_EQ(reader2.GetVisibleChannels().size(), 3);
  EXPECT_EQ(reader1.GetVisibleChannels()[0], 0);
  EXPECT_EQ(reader2.GetVisibleChannels()[0], 2);
}

// ============================================================================
// Use Case Tests
// ============================================================================

TEST(ChannelSelectionTest, SpectralImagingUseCase) {
  MockReaderForChannelSelection reader;

  // Select specific spectral bands for visualization
  std::vector<size_t> spectral_bands = {5, 15, 25};  // e.g., R, G, B mapping
  reader.SetVisibleChannels(spectral_bands);

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 3);
  EXPECT_EQ(channels[0], 5);
  EXPECT_EQ(channels[1], 15);
  EXPECT_EQ(channels[2], 25);
}

TEST(ChannelSelectionTest, FluorescenceImagingUseCase) {
  MockReaderForChannelSelection reader;

  // Select specific fluorescence channels
  std::vector<size_t> fluorescence_channels = {0, 1, 2,
                                               3};  // DAPI, FITC, TRITC, Cy5
  reader.SetVisibleChannels(fluorescence_channels);

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 4);
}

TEST(ChannelSelectionTest, SingleChannelVisualization) {
  MockReaderForChannelSelection reader;

  // View only one channel at a time (common for analysis)
  reader.SetVisibleChannels({0});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 1);
  EXPECT_EQ(channels[0], 0);

  // Switch to different channel
  reader.SetVisibleChannels({1});
  EXPECT_EQ(reader.GetVisibleChannels().size(), 1);
  EXPECT_EQ(reader.GetVisibleChannels()[0], 1);
}

TEST(ChannelSelectionTest, ChannelSubsetForPerformance) {
  MockReaderForChannelSelection reader;

  // Load only subset of channels to improve performance
  // For example, a 16-channel slide but only need 3 for current analysis
  reader.SetVisibleChannels({2, 7, 12});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 3);
  // This should reduce I/O and memory usage
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ChannelSelectionTest, ZeroChannel) {
  MockReaderForChannelSelection reader;

  reader.SetVisibleChannels({0});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 1);
  EXPECT_EQ(channels[0], 0);
}

TEST(ChannelSelectionTest, ConsecutiveOperations) {
  MockReaderForChannelSelection reader;

  // Multiple consecutive SetVisibleChannels calls
  reader.SetVisibleChannels({0});
  reader.SetVisibleChannels({1});
  reader.SetVisibleChannels({2});

  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 1);
  EXPECT_EQ(channels[0], 2);  // Last set wins
}

TEST(ChannelSelectionTest, AlternatingSetAndReset) {
  MockReaderForChannelSelection reader;

  reader.SetVisibleChannels({0, 1, 2});
  EXPECT_FALSE(reader.GetVisibleChannels().empty());

  reader.ShowAllChannels();
  EXPECT_TRUE(reader.GetVisibleChannels().empty());

  reader.SetVisibleChannels({3, 4});
  EXPECT_FALSE(reader.GetVisibleChannels().empty());
  EXPECT_EQ(reader.GetVisibleChannels().size(), 2);
}

// ============================================================================
// Integration with Other SlideReader Methods
// ============================================================================

TEST(ChannelSelectionTest, DoesNotAffectOtherReaderState) {
  MockReaderForChannelSelection reader;

  // Set channel selection
  reader.SetVisibleChannels({0, 1});

  // Verify other reader methods still work
  EXPECT_EQ(reader.GetLevelCount(), 1);
  EXPECT_EQ(reader.GetFormatName(), "MockFormat");
  EXPECT_TRUE(reader.GetLevelInfo(0).ok());
}

TEST(ChannelSelectionTest, PreservedAcrossQueries) {
  MockReaderForChannelSelection reader;

  reader.SetVisibleChannels({2, 3, 4});

  // Make various queries
  auto level_info = reader.GetLevelInfo(0);
  EXPECT_TRUE(level_info.ok());

  auto metadata = reader.GetMetadata();

  // Channel selection should still be set
  const auto& channels = reader.GetVisibleChannels();
  EXPECT_EQ(channels.size(), 3);
  EXPECT_EQ(channels[0], 2);
}

}  // namespace fastslide
