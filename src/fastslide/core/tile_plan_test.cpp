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

#include "fastslide/core/tile_plan.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace fastslide {
namespace core {

// ============================================================================
// TileTransform Tests
// ============================================================================

TEST(TileTransformTest, NoTransformations) {
  TileTransform transform;
  transform.source = {0, 0, 256, 256};
  transform.dest = {0, 0, 256, 256};
  transform.scale_x = 1.0;
  transform.scale_y = 1.0;

  EXPECT_FALSE(transform.NeedsScaling());
  EXPECT_FALSE(transform.NeedsCropping());
}

TEST(TileTransformTest, ScalingDetection) {
  TileTransform transform;
  transform.source = {0, 0, 256, 256};
  transform.dest = {0, 0, 512, 512};
  transform.scale_x = 2.0;
  transform.scale_y = 2.0;

  EXPECT_TRUE(transform.NeedsScaling());
  EXPECT_TRUE(transform.NeedsCropping());  // Size mismatch
}

TEST(TileTransformTest, CroppingDetection) {
  TileTransform transform;
  transform.source = {10, 10, 200, 200};
  transform.dest = {0, 0, 200, 200};
  transform.scale_x = 1.0;
  transform.scale_y = 1.0;

  EXPECT_FALSE(transform.NeedsScaling());
  EXPECT_FALSE(transform.NeedsCropping());  // Same dimensions
}

TEST(TileTransformTest, CroppingWithSizeMismatch) {
  TileTransform transform;
  transform.source = {0, 0, 256, 256};
  transform.dest = {0, 0, 128, 128};
  transform.scale_x = 1.0;
  transform.scale_y = 1.0;

  EXPECT_FALSE(transform.NeedsScaling());
  EXPECT_TRUE(transform.NeedsCropping());
}

TEST(TileTransformTest, PartialTile) {
  // Edge tile that's partially outside image bounds
  TileTransform transform;
  transform.source = {0, 0, 256, 256};
  transform.dest = {3840, 3840, 256, 256};  // Partial tile at edge
  transform.scale_x = 1.0;
  transform.scale_y = 1.0;

  EXPECT_FALSE(transform.NeedsScaling());
  EXPECT_FALSE(transform.NeedsCropping());
}

TEST(TileTransformTest, Downsampling) {
  TileTransform transform;
  transform.source = {0, 0, 512, 512};
  transform.dest = {0, 0, 256, 256};
  transform.scale_x = 0.5;
  transform.scale_y = 0.5;

  EXPECT_TRUE(transform.NeedsScaling());
  EXPECT_TRUE(transform.NeedsCropping());
}

// ============================================================================
// BlendMetadata Tests
// ============================================================================

TEST(BlendMetadataTest, DefaultValues) {
  BlendMetadata metadata;

  EXPECT_DOUBLE_EQ(metadata.fractional_x, 0.0);
  EXPECT_DOUBLE_EQ(metadata.fractional_y, 0.0);
  EXPECT_DOUBLE_EQ(metadata.weight, 1.0);
  EXPECT_EQ(metadata.mode, BlendMode::kOverwrite);
  EXPECT_TRUE(metadata.enable_subpixel_resampling);
}

TEST(BlendMetadataTest, SubpixelPositioning) {
  BlendMetadata metadata;
  metadata.fractional_x = 0.375;
  metadata.fractional_y = 0.625;
  metadata.weight = 1.0;
  metadata.mode = BlendMode::kAverage;

  EXPECT_NEAR(metadata.fractional_x, 0.375, 1e-9);
  EXPECT_NEAR(metadata.fractional_y, 0.625, 1e-9);
  EXPECT_EQ(metadata.mode, BlendMode::kAverage);
}

TEST(BlendMetadataTest, WeightedBlending) {
  BlendMetadata metadata;
  metadata.weight = 0.75;
  metadata.mode = BlendMode::kAverage;

  EXPECT_NEAR(metadata.weight, 0.75, 1e-9);
}

TEST(BlendMetadataTest, DisableSubpixelResampling) {
  BlendMetadata metadata;
  metadata.fractional_x = 0.5;
  metadata.fractional_y = 0.5;
  metadata.enable_subpixel_resampling = false;

  EXPECT_FALSE(metadata.enable_subpixel_resampling);
}

// ============================================================================
// TileReadOp Tests
// ============================================================================

TEST(TileReadOpTest, BasicConstruction) {
  TileReadOp op;
  op.level = 0;
  op.tile_coord = {5, 7};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {1280, 1792, 256, 256};
  op.source_id = 0;
  op.byte_offset = 1024;
  op.byte_size = 65536;
  op.priority = 0;

  EXPECT_EQ(op.level, 0);
  EXPECT_EQ(op.tile_coord.x, 5);
  EXPECT_EQ(op.tile_coord.y, 7);
  EXPECT_EQ(op.source_id, 0);
  EXPECT_EQ(op.byte_offset, 1024);
  EXPECT_EQ(op.byte_size, 65536);
  EXPECT_FALSE(op.blend_metadata.has_value());
}

TEST(TileReadOpTest, WithBlendMetadata) {
  TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 1024;

  BlendMetadata blend;
  blend.fractional_x = 0.25;
  blend.fractional_y = 0.75;
  blend.weight = 1.0;
  blend.mode = BlendMode::kAverage;

  op.blend_metadata = blend;

  ASSERT_TRUE(op.blend_metadata.has_value());
  EXPECT_DOUBLE_EQ(op.blend_metadata->fractional_x, 0.25);
  EXPECT_DOUBLE_EQ(op.blend_metadata->fractional_y, 0.75);
  EXPECT_EQ(op.blend_metadata->mode, BlendMode::kAverage);
}

TEST(TileReadOpTest, Priority) {
  TileReadOp high_priority;
  high_priority.priority = 10;

  TileReadOp low_priority;
  low_priority.priority = 1;

  EXPECT_GT(high_priority.priority, low_priority.priority);
}

TEST(TileReadOpTest, LargeFileOffset) {
  TileReadOp op;
  op.byte_offset = std::numeric_limits<uint64_t>::max() - 1000;
  op.byte_size = 1000;

  EXPECT_GT(op.byte_offset, 1ULL << 40);  // > 1 TB
}

// ============================================================================
// OutputSpec Tests
// ============================================================================

TEST(OutputSpecTest, RGBOutput) {
  OutputSpec spec;
  spec.dimensions = {1024, 768};
  spec.channels = 3;
  spec.pixel_format = OutputSpec::PixelFormat::kUInt8;
  spec.planar_config = PlanarConfig::kContiguous;

  EXPECT_EQ(spec.dimensions[0], 1024);
  EXPECT_EQ(spec.dimensions[1], 768);
  EXPECT_EQ(spec.channels, 3);
  EXPECT_EQ(spec.GetTotalBytes(), 1024 * 768 * 3);
}

TEST(OutputSpecTest, GrayscaleOutput) {
  OutputSpec spec;
  spec.dimensions = {512, 512};
  spec.channels = 1;
  spec.pixel_format = OutputSpec::PixelFormat::kUInt8;

  EXPECT_EQ(spec.GetTotalBytes(), 512 * 512);
}

TEST(OutputSpecTest, UInt16Output) {
  OutputSpec spec;
  spec.dimensions = {256, 256};
  spec.channels = 3;
  spec.pixel_format = OutputSpec::PixelFormat::kUInt16;

  EXPECT_EQ(spec.GetTotalBytes(), 256 * 256 * 3 * 2);  // 2 bytes per pixel
}

TEST(OutputSpecTest, Float32Output) {
  OutputSpec spec;
  spec.dimensions = {128, 128};
  spec.channels = 4;
  spec.pixel_format = OutputSpec::PixelFormat::kFloat32;

  EXPECT_EQ(spec.GetTotalBytes(), 128 * 128 * 4 * 4);  // 4 bytes per pixel
}

TEST(OutputSpecTest, SpectralImaging) {
  OutputSpec spec;
  spec.dimensions = {1024, 1024};
  spec.channels = 16;  // 16-channel spectral data
  spec.pixel_format = OutputSpec::PixelFormat::kUInt16;

  EXPECT_EQ(spec.GetTotalBytes(), 1024 * 1024 * 16 * 2);
}

TEST(OutputSpecTest, BackgroundColor) {
  OutputSpec spec;
  spec.background.r = 255;
  spec.background.g = 240;
  spec.background.b = 230;
  spec.background.a = 255;

  EXPECT_EQ(spec.background.r, 255);
  EXPECT_EQ(spec.background.g, 240);
  EXPECT_EQ(spec.background.b, 230);
  EXPECT_EQ(spec.background.a, 255);
}

TEST(OutputSpecTest, ChannelSelection) {
  OutputSpec spec;
  spec.dimensions = {512, 512};
  spec.channels = 3;
  spec.channel_indices = {0, 2, 4};  // Select specific channels

  EXPECT_EQ(spec.channel_indices.size(), 3);
  EXPECT_EQ(spec.channel_indices[0], 0);
  EXPECT_EQ(spec.channel_indices[1], 2);
  EXPECT_EQ(spec.channel_indices[2], 4);
}

// ============================================================================
// TilePlan Tests
// ============================================================================

TEST(TilePlanTest, EmptyPlan) {
  TilePlan plan;

  EXPECT_FALSE(plan.IsValid());  // No operations
  EXPECT_TRUE(plan.IsEmpty());
  EXPECT_EQ(plan.GetOperationCount(), 0);
}

TEST(TilePlanTest, SingleTilePlan) {
  TilePlan plan;

  // Setup request
  plan.request.level = 0;
  plan.request.tile_coord = {0, 0};

  // Setup output
  plan.output.dimensions = {256, 256};
  plan.output.channels = 3;

  // Setup actual region
  plan.actual_region.top_left = {0, 0};
  plan.actual_region.size = {256, 256};
  plan.actual_region.level = 0;

  // Add operation
  TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 1024;

  plan.operations.push_back(op);

  EXPECT_TRUE(plan.IsValid());
  EXPECT_FALSE(plan.IsEmpty());
  EXPECT_EQ(plan.GetOperationCount(), 1);
}

TEST(TilePlanTest, MultiTilePlan) {
  TilePlan plan;

  plan.request.level = 0;
  plan.output.dimensions = {1024, 1024};
  plan.output.channels = 3;
  plan.actual_region = {{0, 0}, {1024, 1024}, 0};

  // Add multiple tile operations (4x4 grid)
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      TileReadOp op;
      op.level = 0;
      op.tile_coord = {x, y};
      op.transform.source = {0, 0, 256, 256};
      op.transform.dest = {x * 256, y * 256, 256, 256};
      op.source_id = 0;
      op.byte_offset = (y * 4 + x) * 65536;
      op.byte_size = 65536;

      plan.operations.push_back(op);
    }
  }

  EXPECT_TRUE(plan.IsValid());
  EXPECT_FALSE(plan.IsEmpty());
  EXPECT_EQ(plan.GetOperationCount(), 16);
}

TEST(TilePlanTest, CostEstimation) {
  TilePlan plan;

  plan.request.level = 0;
  plan.output.dimensions = {512, 512};
  plan.output.channels = 3;
  plan.actual_region = {{0, 0}, {512, 512}, 0};

  // Add operations with cost information
  for (int i = 0; i < 4; ++i) {
    TileReadOp op;
    op.level = 0;
    op.byte_offset = i * 10000;
    op.byte_size = 10000;
    plan.operations.push_back(op);
  }

  // Set cost metrics
  plan.cost.total_bytes_to_read = 40000;
  plan.cost.total_tiles = 4;
  plan.cost.tiles_to_decode = 4;
  plan.cost.tiles_from_cache = 0;
  plan.cost.estimated_time_ms = 10.0;

  EXPECT_EQ(plan.cost.total_bytes_to_read, 40000);
  EXPECT_EQ(plan.cost.total_tiles, 4);
  EXPECT_EQ(plan.cost.tiles_to_decode, 4);
  EXPECT_EQ(plan.cost.tiles_from_cache, 0);
  EXPECT_DOUBLE_EQ(plan.cost.estimated_time_ms, 10.0);
}

TEST(TilePlanTest, CachedTiles) {
  TilePlan plan;

  plan.request.level = 0;
  plan.output.dimensions = {512, 512};
  plan.output.channels = 3;
  plan.actual_region = {{0, 0}, {512, 512}, 0};

  // Add operations
  for (int i = 0; i < 10; ++i) {
    TileReadOp op;
    op.level = 0;
    op.byte_size = 10000;
    plan.operations.push_back(op);
  }

  // Some tiles are in cache
  plan.cost.total_tiles = 10;
  plan.cost.tiles_from_cache = 6;
  plan.cost.tiles_to_decode = 4;

  EXPECT_EQ(plan.cost.tiles_from_cache, 6);
  EXPECT_EQ(plan.cost.tiles_to_decode, 4);
}

TEST(TilePlanTest, InvalidPlanMissingRegion) {
  TilePlan plan;

  // Valid operations but invalid region
  TileReadOp op;
  op.level = 0;
  op.byte_size = 1000;
  plan.operations.push_back(op);

  plan.actual_region = {{0, 0}, {0, 0}, 0};  // Invalid size

  EXPECT_FALSE(plan.IsValid());
}

TEST(TilePlanTest, PlanWithBlending) {
  TilePlan plan;

  plan.request.level = 0;
  plan.output.dimensions = {512, 512};
  plan.output.channels = 3;
  plan.actual_region = {{0, 0}, {512, 512}, 0};

  // Add overlapping tiles with blend metadata (MRXS-style)
  TileReadOp op1;
  op1.level = 0;
  op1.tile_coord = {0, 0};
  op1.transform.dest = {0, 0, 256, 256};
  op1.byte_size = 1000;

  BlendMetadata blend1;
  blend1.fractional_x = 0.0;
  blend1.fractional_y = 0.0;
  blend1.mode = BlendMode::kAverage;
  op1.blend_metadata = blend1;

  TileReadOp op2;
  op2.level = 0;
  op2.tile_coord = {1, 0};
  op2.transform.dest = {240, 0, 256, 256};  // 16-pixel overlap
  op2.byte_size = 1000;

  BlendMetadata blend2;
  blend2.fractional_x = 0.5;
  blend2.fractional_y = 0.0;
  blend2.mode = BlendMode::kAverage;
  op2.blend_metadata = blend2;

  plan.operations.push_back(op1);
  plan.operations.push_back(op2);

  EXPECT_TRUE(plan.IsValid());
  EXPECT_EQ(plan.GetOperationCount(), 2);

  // Verify both operations have blend metadata
  EXPECT_TRUE(plan.operations[0].blend_metadata.has_value());
  EXPECT_TRUE(plan.operations[1].blend_metadata.has_value());
}

// ============================================================================
// BatchTilePlan Tests
// ============================================================================

TEST(BatchTilePlanTest, EmptyBatch) {
  BatchTilePlan batch;

  EXPECT_EQ(batch.plans.size(), 0);
  EXPECT_EQ(batch.unique_operations.size(), 0);
  EXPECT_EQ(batch.GetUniqueOperations(), 0);
}

TEST(BatchTilePlanTest, SinglePlanBatch) {
  BatchTilePlan batch;

  TilePlan plan;
  plan.actual_region = {{0, 0}, {256, 256}, 0};
  plan.output.dimensions = {256, 256};
  plan.output.channels = 3;

  TileReadOp op;
  op.byte_size = 1000;
  plan.operations.push_back(op);

  batch.plans.push_back(plan);

  EXPECT_EQ(batch.plans.size(), 1);
}

TEST(BatchTilePlanTest, MultiplePlans) {
  BatchTilePlan batch;

  // Create multiple plans
  for (int i = 0; i < 5; ++i) {
    TilePlan plan;
    plan.actual_region = {{static_cast<uint32_t>(i * 256), 0}, {256, 256}, 0};
    plan.output.dimensions = {256, 256};
    plan.output.channels = 3;

    TileReadOp op;
    op.byte_offset = i * 1000;
    op.byte_size = 1000;
    plan.operations.push_back(op);

    batch.plans.push_back(plan);
  }

  EXPECT_EQ(batch.plans.size(), 5);
}

TEST(BatchTilePlanTest, DeduplicatedOperations) {
  BatchTilePlan batch;

  // Create plans that share some tiles
  TilePlan plan1;
  plan1.actual_region = {{0, 0}, {256, 256}, 0};
  plan1.output.dimensions = {256, 256};
  plan1.output.channels = 3;

  TileReadOp op1;
  op1.tile_coord = {0, 0};
  op1.byte_offset = 0;
  op1.byte_size = 1000;
  plan1.operations.push_back(op1);

  TilePlan plan2;
  plan2.actual_region = {{128, 128}, {256, 256}, 0};
  plan2.output.dimensions = {256, 256};
  plan2.output.channels = 3;

  // Overlapping tile
  TileReadOp op2;
  op2.tile_coord = {0, 0};  // Same tile
  op2.byte_offset = 0;
  op2.byte_size = 1000;
  plan2.operations.push_back(op2);

  batch.plans.push_back(plan1);
  batch.plans.push_back(plan2);

  // Manually add unique operation (simulating deduplication)
  batch.unique_operations.push_back(op1);

  EXPECT_EQ(batch.plans.size(), 2);
  EXPECT_EQ(batch.GetUniqueOperations(), 1);  // Only one unique tile
}

}  // namespace core
}  // namespace fastslide
