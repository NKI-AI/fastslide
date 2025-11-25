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

#include "fastslide/runtime/tile_writer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

namespace fastslide {
namespace runtime {

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Create a simple RGB tile plan for testing
core::TilePlan CreateSimpleRGBPlan(uint32_t width, uint32_t height,
                                   bool enable_blending = false) {
  core::TilePlan plan;

  plan.request.level = 0;
  plan.request.tile_coord = {0, 0};

  plan.output.dimensions = {width, height};
  plan.output.channels = 3;
  plan.output.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  plan.output.planar_config = PlanarConfig::kContiguous;
  plan.output.background = {255, 255, 255, 255};

  plan.actual_region = {{0, 0}, {width, height}, 0};

  // Add a simple tile operation
  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, width, height};
  op.transform.dest = {0, 0, width, height};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = width * height * 3;

  if (enable_blending) {
    core::BlendMetadata blend;
    blend.fractional_x = 0.5;
    blend.fractional_y = 0.5;
    blend.mode = core::BlendMode::kAverage;
    op.blend_metadata = blend;
  }

  plan.operations.push_back(op);
  plan.cost.total_tiles = 1;

  return plan;
}

/// @brief Create test pixel data (gradient pattern)
std::vector<uint8_t> CreateTestPixelData(uint32_t width, uint32_t height,
                                         uint32_t channels) {
  std::vector<uint8_t> data(width * height * channels);

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t pixel_idx = (y * width + x) * channels;

      // Create gradient pattern
      data[pixel_idx + 0] = static_cast<uint8_t>(x % 256);  // R
      if (channels > 1)
        data[pixel_idx + 1] = static_cast<uint8_t>(y % 256);  // G
      if (channels > 2)
        data[pixel_idx + 2] = static_cast<uint8_t>((x + y) % 256);  // B
    }
  }

  return data;
}

// ============================================================================
// TileWriter Construction Tests
// ============================================================================

TEST(TileWriterTest, ConstructFromPlanRGB) {
  auto plan = CreateSimpleRGBPlan(256, 256);

  TileWriter writer(plan);

  EXPECT_EQ(writer.GetDimensions()[0], 256);
  EXPECT_EQ(writer.GetDimensions()[1], 256);
  EXPECT_EQ(writer.GetChannels(), 3);
  EXPECT_FALSE(writer.IsBlendingEnabled());
  EXPECT_EQ(writer.GetStrategyName(), "NativeDirect");
}

TEST(TileWriterTest, ConstructFromPlanWithBlending) {
  auto plan = CreateSimpleRGBPlan(256, 256, true);

  TileWriter writer(plan);

  EXPECT_EQ(writer.GetDimensions()[0], 256);
  EXPECT_EQ(writer.GetDimensions()[1], 256);
  EXPECT_EQ(writer.GetChannels(), 3);
  EXPECT_TRUE(writer.IsBlendingEnabled());
  EXPECT_EQ(writer.GetStrategyName(), "NativeBlended");
}

TEST(TileWriterTest, ConstructWithConfig) {
  TileWriter::Config config;
  config.dimensions = {512, 512};
  config.channels = 3;
  config.data_type = DataType::kUInt8;
  config.enable_blending = false;
  config.background = TileWriter::BackgroundColor(255, 255, 255);

  TileWriter writer(config);

  EXPECT_EQ(writer.GetDimensions()[0], 512);
  EXPECT_EQ(writer.GetDimensions()[1], 512);
  EXPECT_EQ(writer.GetChannels(), 3);
  EXPECT_FALSE(writer.IsBlendingEnabled());
}

TEST(TileWriterTest, ConstructRGBConvenience) {
  TileWriter writer(1024, 768);

  EXPECT_EQ(writer.GetDimensions()[0], 1024);
  EXPECT_EQ(writer.GetDimensions()[1], 768);
  EXPECT_EQ(writer.GetChannels(), 3);
  EXPECT_FALSE(writer.IsBlendingEnabled());
}

TEST(TileWriterTest, ConstructRGBWithBlending) {
  TileWriter writer(256, 256, TileWriter::BackgroundColor(255, 255, 255), true);

  EXPECT_EQ(writer.GetDimensions()[0], 256);
  EXPECT_EQ(writer.GetDimensions()[1], 256);
  EXPECT_EQ(writer.GetChannels(), 3);
  EXPECT_TRUE(writer.IsBlendingEnabled());
}

// ============================================================================
// WriteTile Tests (Direct Strategy)
// ============================================================================

TEST(TileWriterTest, WriteSingleTileRGB) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  TileWriter writer(plan);

  // Create test pixel data
  auto pixel_data = CreateTestPixelData(256, 256, 3);

  // Write tile
  const auto& op = plan.operations[0];
  auto status = writer.WriteTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status;

  // Finalize
  status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  // Get output
  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& output = result.value();
  EXPECT_EQ(output.GetDimensions()[0], 256);
  EXPECT_EQ(output.GetDimensions()[1], 256);
  EXPECT_EQ(output.GetChannels(), 3);
  EXPECT_FALSE(output.Empty());
}

TEST(TileWriterTest, WritePartialTile) {
  auto plan = CreateSimpleRGBPlan(512, 512);
  TileWriter writer(plan);

  // Create partial tile that only covers part of the output
  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};  // Top-left quadrant
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  auto status = writer.WriteTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status;

  status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();
}

TEST(TileWriterTest, WriteMultipleTiles) {
  auto plan = CreateSimpleRGBPlan(512, 512);
  TileWriter writer(plan);

  // Write 4 tiles (2x2 grid)
  for (uint32_t ty = 0; ty < 2; ++ty) {
    for (uint32_t tx = 0; tx < 2; ++tx) {
      core::TileReadOp op;
      op.level = 0;
      op.tile_coord = {tx, ty};
      op.transform.source = {0, 0, 256, 256};
      op.transform.dest = {tx * 256, ty * 256, 256, 256};
      op.source_id = 0;
      op.byte_offset = 0;
      op.byte_size = 256 * 256 * 3;

      auto pixel_data = CreateTestPixelData(256, 256, 3);

      auto status = writer.WriteTile(op, pixel_data, 256, 256, 3);
      ASSERT_TRUE(status.ok()) << status;
    }
  }

  auto status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& output = result.value();
  EXPECT_EQ(output.GetDimensions()[0], 512);
  EXPECT_EQ(output.GetDimensions()[1], 512);
}

TEST(TileWriterTest, WriteTileWithCropping) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  TileWriter writer(plan);

  // Write only part of a tile
  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {64, 64, 128, 128};  // Crop from middle
  op.transform.dest = {0, 0, 128, 128};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  auto status = writer.WriteTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status;

  status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();
}

// ============================================================================
// Blended Strategy Tests
// ============================================================================

TEST(TileWriterTest, BlendedStrategyDetection) {
  auto plan = CreateSimpleRGBPlan(256, 256, true);  // Enable blending

  TileWriter writer(plan);

  EXPECT_TRUE(writer.IsBlendingEnabled());
  EXPECT_EQ(writer.GetStrategyName(), "NativeBlended");
}

TEST(TileWriterTest, BlendedWriteTile) {
  auto plan = CreateSimpleRGBPlan(256, 256, true);
  TileWriter writer(plan);

  // Create tile with blend metadata
  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  core::BlendMetadata blend;
  blend.fractional_x = 0.25;
  blend.fractional_y = 0.75;
  blend.weight = 1.0;
  blend.mode = core::BlendMode::kAverage;
  op.blend_metadata = blend;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  auto status = writer.WriteTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status;

  status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();
}

TEST(TileWriterTest, BlendedOverlappingTiles) {
  auto plan = CreateSimpleRGBPlan(512, 256, true);
  TileWriter writer(plan);

  // Write two overlapping tiles (simulate MRXS-style overlap)
  core::TileReadOp op1;
  op1.level = 0;
  op1.tile_coord = {0, 0};
  op1.transform.source = {0, 0, 256, 256};
  op1.transform.dest = {0, 0, 256, 256};
  op1.source_id = 0;
  op1.byte_offset = 0;
  op1.byte_size = 256 * 256 * 3;

  core::BlendMetadata blend1;
  blend1.weight = 1.0;
  blend1.mode = core::BlendMode::kAverage;
  op1.blend_metadata = blend1;

  auto pixel_data1 = CreateTestPixelData(256, 256, 3);
  auto status = writer.WriteTile(op1, pixel_data1, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status;

  // Second tile with 16-pixel overlap
  core::TileReadOp op2;
  op2.level = 0;
  op2.tile_coord = {1, 0};
  op2.transform.source = {0, 0, 256, 256};
  op2.transform.dest = {240, 0, 256, 256};  // Overlaps at x=240-256
  op2.source_id = 0;
  op2.byte_offset = 0;
  op2.byte_size = 256 * 256 * 3;

  core::BlendMetadata blend2;
  blend2.weight = 1.0;
  blend2.mode = core::BlendMode::kAverage;
  op2.blend_metadata = blend2;

  auto pixel_data2 = CreateTestPixelData(256, 256, 3);
  status = writer.WriteTile(op2, pixel_data2, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status;

  status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(TileWriterTest, OutOfBoundsWrite) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  TileWriter writer(plan);

  // Try to write tile beyond image bounds
  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {512, 512, 256, 256};  // Way out of bounds
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  auto status = writer.WriteTile(op, pixel_data, 256, 256, 3);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kOutOfRange);
}

TEST(TileWriterTest, InsufficientPixelData) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  TileWriter writer(plan);

  // Provide insufficient pixel data
  std::vector<uint8_t> insufficient_data(100);  // Way too small

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto status = writer.WriteTile(op, insufficient_data, 256, 256, 3);
  // Should handle gracefully (won't crash, may clip or error)
  // Exact behavior depends on implementation
}

TEST(TileWriterTest, GetOutputBeforeFinalize) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  TileWriter writer(plan);

  // Try to get output before finalize
  auto result = writer.GetOutput();

  // Should either auto-finalize or succeed (depends on strategy)
  EXPECT_TRUE(result.ok()) << result.status();
}

// ============================================================================
// Multi-Channel Tests
// ============================================================================

TEST(TileWriterTest, GrayscaleOutput) {
  core::TilePlan plan;
  plan.output.dimensions = {256, 256};
  plan.output.channels = 1;
  plan.output.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  plan.output.background = {128, 128, 128, 255};
  plan.actual_region = {{0, 0}, {256, 256}, 0};

  core::TileReadOp op;
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.byte_size = 256 * 256;
  plan.operations.push_back(op);

  TileWriter writer(plan);

  EXPECT_EQ(writer.GetChannels(), 1);

  auto pixel_data = CreateTestPixelData(256, 256, 1);
  auto status = writer.WriteTile(op, pixel_data, 256, 256, 1);
  ASSERT_TRUE(status.ok()) << status;

  status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();
}

TEST(TileWriterTest, RGBAOutput) {
  core::TilePlan plan;
  plan.output.dimensions = {256, 256};
  plan.output.channels = 4;
  plan.output.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  plan.output.background = {255, 255, 255, 255};
  plan.actual_region = {{0, 0}, {256, 256}, 0};

  core::TileReadOp op;
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.byte_size = 256 * 256 * 4;
  plan.operations.push_back(op);

  TileWriter writer(plan);

  EXPECT_EQ(writer.GetChannels(), 4);

  auto pixel_data = CreateTestPixelData(256, 256, 4);
  auto status = writer.WriteTile(op, pixel_data, 256, 256, 4);
  ASSERT_TRUE(status.ok()) << status;

  status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();
}

// ============================================================================
// Performance / Stress Tests
// ============================================================================

TEST(TileWriterTest, LargeImage) {
  auto plan = CreateSimpleRGBPlan(4096, 4096);
  TileWriter writer(plan);

  // Write tiles in 256x256 chunks
  const uint32_t tile_size = 256;
  const uint32_t tiles_x = 4096 / tile_size;  // 16
  const uint32_t tiles_y = 4096 / tile_size;  // 16

  for (uint32_t ty = 0; ty < tiles_y; ++ty) {
    for (uint32_t tx = 0; tx < tiles_x; ++tx) {
      core::TileReadOp op;
      op.level = 0;
      op.tile_coord = {tx, ty};
      op.transform.source = {0, 0, tile_size, tile_size};
      op.transform.dest = {tx * tile_size, ty * tile_size, tile_size,
                           tile_size};
      op.source_id = 0;
      op.byte_offset = 0;
      op.byte_size = tile_size * tile_size * 3;

      auto pixel_data = CreateTestPixelData(tile_size, tile_size, 3);

      auto status = writer.WriteTile(op, pixel_data, tile_size, tile_size, 3);
      ASSERT_TRUE(status.ok()) << status;
    }
  }

  auto status = writer.Finalize();
  ASSERT_TRUE(status.ok()) << status;

  auto result = writer.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status();

  const auto& output = result.value();
  EXPECT_EQ(output.GetDimensions()[0], 4096);
  EXPECT_EQ(output.GetDimensions()[1], 4096);
  EXPECT_EQ(output.SizeBytes(), 4096 * 4096 * 3);
}

}  // namespace runtime
}  // namespace fastslide
