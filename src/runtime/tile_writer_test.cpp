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
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

namespace fastslide {
namespace runtime {

// ============================================================================
// Helper Functions
// ============================================================================

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
    blend.mode = core::BlendMode::kAverage;
    op.blend_metadata = blend;
  }

  plan.operations.push_back(op);
  plan.cost.total_tiles = 1;

  return plan;
}

std::vector<uint8_t> CreateTestPixelData(uint32_t width, uint32_t height,
                                         uint32_t channels) {
  std::vector<uint8_t> data(width * height * channels);

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t pixel_idx = (y * width + x) * channels;

      data[pixel_idx + 0] = static_cast<uint8_t>(x % 256);
      if (channels > 1)
        data[pixel_idx + 1] = static_cast<uint8_t>(y % 256);
      if (channels > 2)
        data[pixel_idx + 2] = static_cast<uint8_t>((x + y) % 256);
      if (channels > 3)
        data[pixel_idx + 3] = 255;
    }
  }

  return data;
}

// ============================================================================
// Canvas Construction Tests
// ============================================================================

TEST(CanvasTest, ConstructFromPlanRGB) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  EXPECT_EQ(canvas.GetDimensions()[0], 256);
  EXPECT_EQ(canvas.GetDimensions()[1], 256);
  EXPECT_EQ(canvas.GetChannels(), 3);
}

TEST(CanvasTest, ConstructFromPlanWithBlending) {
  auto plan = CreateSimpleRGBPlan(256, 256, true);
  Canvas canvas(plan);

  EXPECT_EQ(canvas.GetDimensions()[0], 256);
  EXPECT_EQ(canvas.GetDimensions()[1], 256);
  EXPECT_EQ(canvas.GetChannels(), 3);
  EXPECT_TRUE(canvas.IsBlendingEnabled());
}

TEST(CanvasTest, ConstructWithConfig) {
  Canvas::Config config;
  config.dimensions = {512, 512};
  config.channels = 3;
  config.data_type = DataType::kUInt8;
  config.enable_blending = false;
  config.background = Canvas::BackgroundColor(255, 255, 255);

  Canvas canvas(config);

  EXPECT_EQ(canvas.GetDimensions()[0], 512);
  EXPECT_EQ(canvas.GetDimensions()[1], 512);
  EXPECT_EQ(canvas.GetChannels(), 3);
}

TEST(CanvasTest, ConstructRGBConvenience) {
  Canvas canvas(ImageDimensions{1024, 768});

  EXPECT_EQ(canvas.GetDimensions()[0], 1024);
  EXPECT_EQ(canvas.GetDimensions()[1], 768);
  EXPECT_EQ(canvas.GetChannels(), 3);
}

TEST(CanvasTest, ConstructRGBWithBlending) {
  Canvas canvas(ImageDimensions{256, 256},
                Canvas::BackgroundColor(255, 255, 255), true);

  EXPECT_EQ(canvas.GetDimensions()[0], 256);
  EXPECT_EQ(canvas.GetDimensions()[1], 256);
  EXPECT_EQ(canvas.GetChannels(), 3);
  EXPECT_TRUE(canvas.IsBlendingEnabled());
}

// ============================================================================
// PaintTile Tests
// ============================================================================

TEST(CanvasTest, PaintSingleTileRGB) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  const auto& op = plan.operations[0];
  auto status = canvas.PaintTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status.ToString();

  status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  const auto& output = result.value();
  EXPECT_EQ(output.GetDimensions()[0], 256);
  EXPECT_EQ(output.GetDimensions()[1], 256);
  EXPECT_EQ(output.GetChannels(), 3);
  EXPECT_FALSE(output.Empty());
}

/// @brief A tile buffer shorter than its declared geometry must be refused.
///
/// The paint sinks derive every source offset from the declared tile geometry,
/// which comes from file metadata. A malicious or corrupt slide can declare a
/// larger tile than it actually stores; without this check the sinks read past
/// the end of the decoded buffer and the stale bytes reach the caller.
TEST(CanvasTest, RejectsTileBufferShorterThanDeclaredGeometry) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  // Declare a 256x256x3 tile but supply one byte less than that needs.
  std::vector<uint8_t> truncated(256U * 256U * 3U - 1U, 0);

  const auto& op = plan.operations[0];
  const auto status = canvas.PaintTile(op, truncated, 256, 256, 3);
  EXPECT_FALSE(status.ok());
}

TEST(CanvasTest, RejectsTileDeclaringZeroChannels) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  auto pixel_data = CreateTestPixelData(256, 256, 3);
  const auto& op = plan.operations[0];
  EXPECT_FALSE(canvas.PaintTile(op, pixel_data, 256, 256, 0).ok());
}

TEST(CanvasTest, PaintPartialTile) {
  auto plan = CreateSimpleRGBPlan(512, 512);
  Canvas canvas(plan);

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  auto status = canvas.PaintTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status.ToString();

  status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();
}

TEST(CanvasTest, PaintMultipleTiles) {
  auto plan = CreateSimpleRGBPlan(512, 512);
  Canvas canvas(plan);

  for (uint32_t ty = 0; ty < 2; ++ty) {
    for (uint32_t tx = 0; tx < 2; ++tx) {
      core::TileReadOp op;
      op.level = 0;
      op.tile_coord = {tx, ty};
      op.transform.source = {0, 0, 256, 256};
      op.transform.dest = {static_cast<double>(tx * 256),
                           static_cast<double>(ty * 256), 256, 256};
      op.source_id = 0;
      op.byte_offset = 0;
      op.byte_size = 256 * 256 * 3;

      auto pixel_data = CreateTestPixelData(256, 256, 3);
      auto status = canvas.PaintTile(op, pixel_data, 256, 256, 3);
      ASSERT_TRUE(status.ok()) << status.ToString();
    }
  }

  auto status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  const auto& output = result.value();
  EXPECT_EQ(output.GetDimensions()[0], 512);
  EXPECT_EQ(output.GetDimensions()[1], 512);
}

TEST(CanvasTest, PaintTileWithCropping) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {64, 64, 128, 128};
  op.transform.dest = {0, 0, 128, 128};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  auto status = canvas.PaintTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status.ToString();

  status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();
}

// ============================================================================
// Blended / Overlapping Tile Tests
// ============================================================================

TEST(CanvasTest, BlendingEnabled) {
  auto plan = CreateSimpleRGBPlan(256, 256, true);
  Canvas canvas(plan);

  EXPECT_TRUE(canvas.IsBlendingEnabled());
}

TEST(CanvasTest, FractionalPositionPaint) {
  auto plan = CreateSimpleRGBPlan(256, 256, true);
  Canvas canvas(plan);

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  // Use fractional dest coordinates for sub-pixel placement.
  op.transform.dest = {0.25, 0.75, 256, 256};

  core::BlendMetadata blend;
  blend.weight = 1.0;
  blend.mode = core::BlendMode::kAverage;
  op.blend_metadata = blend;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  auto status = canvas.PaintTile(op, pixel_data, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status.ToString();

  status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  const auto& output = result.value();
  EXPECT_EQ(output.GetDimensions()[0], 256);
  EXPECT_EQ(output.GetDimensions()[1], 256);
  EXPECT_EQ(output.GetChannels(), 3);
}

TEST(CanvasTest, OverlappingTiles) {
  auto plan = CreateSimpleRGBPlan(512, 256, true);
  Canvas canvas(plan);

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
  auto status = canvas.PaintTile(op1, pixel_data1, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status.ToString();

  core::TileReadOp op2;
  op2.level = 0;
  op2.tile_coord = {1, 0};
  op2.transform.source = {0, 0, 256, 256};
  op2.transform.dest = {240, 0, 256, 256};
  op2.source_id = 0;
  op2.byte_offset = 0;
  op2.byte_size = 256 * 256 * 3;

  core::BlendMetadata blend2;
  blend2.weight = 1.0;
  blend2.mode = core::BlendMode::kAverage;
  op2.blend_metadata = blend2;

  auto pixel_data2 = CreateTestPixelData(256, 256, 3);
  status = canvas.PaintTile(op2, pixel_data2, 256, 256, 3);
  ASSERT_TRUE(status.ok()) << status.ToString();

  status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(CanvasTest, OutOfBoundsPaintClips) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {512, 512, 256, 256};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto pixel_data = CreateTestPixelData(256, 256, 3);

  // Out-of-bounds paints are silently clipped -- the tile is just invisible.
  auto status = canvas.PaintTile(op, pixel_data, 256, 256, 3);
  EXPECT_TRUE(status.ok());
}

TEST(CanvasTest, InsufficientPixelData) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  std::vector<uint8_t> insufficient_data(100);

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 256, 256};
  op.transform.dest = {0, 0, 256, 256};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = 256 * 256 * 3;

  auto status = canvas.PaintTile(op, insufficient_data, 256, 256, 3);
  EXPECT_FALSE(status.ok());
}

TEST(CanvasTest, GetOutputBeforeFinalize) {
  auto plan = CreateSimpleRGBPlan(256, 256);
  Canvas canvas(plan);

  auto result = canvas.GetOutput();
  EXPECT_TRUE(result.ok()) << result.status().ToString();
}

// ============================================================================
// Multi-Channel Tests
// ============================================================================

TEST(CanvasTest, GrayscaleOutput) {
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

  Canvas canvas(plan);
  EXPECT_EQ(canvas.GetChannels(), 1);

  auto pixel_data = CreateTestPixelData(256, 256, 1);
  auto status = canvas.PaintTile(op, pixel_data, 256, 256, 1);
  ASSERT_TRUE(status.ok()) << status.ToString();

  status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();
}

TEST(CanvasTest, RGBAOutput) {
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

  Canvas canvas(plan);
  EXPECT_EQ(canvas.GetChannels(), 4);

  auto pixel_data = CreateTestPixelData(256, 256, 4);
  auto status = canvas.PaintTile(op, pixel_data, 256, 256, 4);
  ASSERT_TRUE(status.ok()) << status.ToString();

  status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();
}

TEST(CanvasTest, SpectralOutput) {
  Canvas::Config config;
  config.dimensions = {64, 64};
  config.channels = 7;
  config.data_type = DataType::kUInt8;
  config.background = Canvas::BackgroundColor(std::vector<double>(7, 0.0));

  Canvas canvas(config);
  EXPECT_EQ(canvas.GetChannels(), 7);
}

// ============================================================================
// Performance / Stress Tests
// ============================================================================

TEST(CanvasTest, LargeImage) {
  auto plan = CreateSimpleRGBPlan(4096, 4096);
  Canvas canvas(plan);

  const uint32_t tile_size = 256;
  const uint32_t tiles_x = 4096 / tile_size;
  const uint32_t tiles_y = 4096 / tile_size;

  for (uint32_t ty = 0; ty < tiles_y; ++ty) {
    for (uint32_t tx = 0; tx < tiles_x; ++tx) {
      core::TileReadOp op;
      op.level = 0;
      op.tile_coord = {tx, ty};
      op.transform.source = {0, 0, tile_size, tile_size};
      op.transform.dest = {static_cast<double>(tx * tile_size),
                           static_cast<double>(ty * tile_size), tile_size,
                           tile_size};
      op.source_id = 0;
      op.byte_offset = 0;
      op.byte_size = tile_size * tile_size * 3;

      auto pixel_data = CreateTestPixelData(tile_size, tile_size, 3);

      auto status = canvas.PaintTile(op, pixel_data, tile_size, tile_size, 3);
      ASSERT_TRUE(status.ok()) << status.ToString();
    }
  }

  auto status = canvas.Finalize();
  ASSERT_TRUE(status.ok()) << status.ToString();

  auto result = canvas.GetOutput();
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  const auto& output = result.value();
  EXPECT_EQ(output.GetDimensions()[0], 4096);
  EXPECT_EQ(output.GetDimensions()[1], 4096);
  EXPECT_EQ(output.SizeBytes(), 4096 * 4096 * 3);
}

// ============================================================================
// Painting by row bands
// ============================================================================

namespace {

struct BandTestTile {
  core::TileReadOp op;
  std::vector<uint8_t> data;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t channels = 0;
};

/// A tile of random samples, read from source rectangle (src_x, src_y,
/// w, h) and painted at (dest_x, dest_y).
BandTestTile MakeBandTestTile(uint32_t tile_w, uint32_t tile_h,
                              uint32_t channels, uint32_t bytes_per_sample,
                              double src_x, double src_y, uint32_t w,
                              uint32_t h, double dest_x, double dest_y,
                              float gain, uint32_t seed) {
  BandTestTile tile;
  tile.width = tile_w;
  tile.height = tile_h;
  tile.channels = channels;
  tile.data.resize(static_cast<size_t>(tile_w) * tile_h * channels *
                   bytes_per_sample);
  std::mt19937 rng(seed);
  for (auto& byte : tile.data) {
    byte = static_cast<uint8_t>(rng());
  }
  tile.op.level = 0;
  tile.op.tile_coord = {0, 0};
  tile.op.transform.source = {src_x, src_y, w, h};
  tile.op.transform.dest = {dest_x, dest_y, w, h};
  core::BlendMetadata blend;
  blend.gain = gain;
  tile.op.blend_metadata = blend;
  return tile;
}

/// Paints `tiles` whole, in order, on one canvas, and band by band on
/// another, every band on its own thread and in the same order; both must
/// hold the same bytes.
void ExpectBandsMatchWholePaint(const Canvas::Config& config,
                                const std::vector<BandTestTile>& tiles,
                                uint32_t band_rows) {
  Canvas whole(config);
  for (const auto& tile : tiles) {
    ASSERT_TRUE(whole
                    .PaintTile(tile.op, tile.data, tile.width, tile.height,
                               tile.channels)
                    .ok());
  }

  Canvas banded(config);
  std::vector<std::thread> threads;
  for (uint32_t begin = 0; begin < config.dimensions[1]; begin += band_rows) {
    threads.emplace_back([&, begin] {
      for (const auto& tile : tiles) {
        EXPECT_TRUE(banded
                        .PaintTileRows(tile.op, tile.data, tile.width,
                                       tile.height, tile.channels, begin,
                                       begin + band_rows)
                        .ok());
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  auto expected = whole.GetOutput();
  auto actual = banded.GetOutput();
  ASSERT_TRUE(expected.ok() && actual.ok());
  ASSERT_EQ(expected->SizeBytes(), actual->SizeBytes());
  EXPECT_EQ(std::memcmp(expected->GetData(), actual->GetData(),
                        expected->SizeBytes()),
            0)
      << "bands of " << band_rows << " rows";
}

Canvas::Config BandTestConfig(uint32_t channels, DataType data_type,
                              PlanarConfig planar_config) {
  Canvas::Config config;
  config.dimensions = {97, 83};
  config.channels = channels;
  config.data_type = data_type;
  config.planar_config = planar_config;
  return config;
}

}  // namespace

TEST(CanvasTest, PaintTileRowsMatchesPaintTileRgb8) {
  // Overlapping tiles at integer and sub-pixel positions, with and without
  // gain, cropped from a larger stored image, and partly off the canvas.
  const std::vector<BandTestTile> tiles = {
      MakeBandTestTile(40, 30, 3, 1, 0, 0, 40, 30, 0, 0, 1.0F, 1),
      MakeBandTestTile(48, 40, 3, 1, 5, 7, 30, 20, 30, 20, 1.0F, 2),
      MakeBandTestTile(50, 40, 3, 1, 0, 0, 50, 40, 10.25, 33.75, 1.3F, 3),
      MakeBandTestTile(64, 48, 3, 1, 8, 6, 32, 24, -3.5, 60.4, 0.8F, 4),
      MakeBandTestTile(40, 30, 3, 1, 0, 0, 40, 30, 60, 70, 1.1F, 5),
      MakeBandTestTile(40, 30, 3, 1, 0, 0, 40, 30, 70.5, -12.25, 1.0F, 6),
  };
  const auto config =
      BandTestConfig(3, DataType::kUInt8, PlanarConfig::kContiguous);
  for (uint32_t band_rows : {1U, 7U, 64U, 83U}) {
    ExpectBandsMatchWholePaint(config, tiles, band_rows);
  }
}

TEST(CanvasTest, PaintTileRowsMatchesPaintTileRgb16) {
  const std::vector<BandTestTile> tiles = {
      MakeBandTestTile(40, 30, 3, 2, 0, 0, 40, 30, 0, 0, 1.0F, 1),
      MakeBandTestTile(48, 40, 3, 2, 5, 7, 30, 20, 30, 20, 1.0F, 2),
      MakeBandTestTile(50, 40, 3, 2, 0, 0, 50, 40, 10.25, 33.75, 1.0F, 3),
      MakeBandTestTile(40, 30, 3, 2, 0, 0, 40, 30, 60, 70, 1.0F, 5),
  };
  const auto config =
      BandTestConfig(3, DataType::kUInt16, PlanarConfig::kContiguous);
  for (uint32_t band_rows : {1U, 7U, 64U}) {
    ExpectBandsMatchWholePaint(config, tiles, band_rows);
  }
}

TEST(CanvasTest, PaintTileRowsMatchesPaintTileInterleaved) {
  const std::vector<BandTestTile> tiles = {
      MakeBandTestTile(40, 30, 4, 1, 0, 0, 40, 30, 0, 0, 1.0F, 1),
      MakeBandTestTile(48, 40, 4, 1, 5, 7, 30, 20, 30, 20, 1.0F, 2),
      MakeBandTestTile(40, 30, 4, 1, 0, 0, 40, 30, 60, 70, 1.0F, 5),
  };
  const auto config =
      BandTestConfig(4, DataType::kUInt8, PlanarConfig::kContiguous);
  for (uint32_t band_rows : {1U, 7U, 64U}) {
    ExpectBandsMatchWholePaint(config, tiles, band_rows);
  }
}

TEST(CanvasTest, PaintTileRowsMatchesPaintTilePlanar) {
  // Single-channel tiles; PaintTilePlanar takes the target channel from
  // tile_coord.x.
  std::vector<BandTestTile> tiles;
  for (uint32_t channel = 0; channel < 5; ++channel) {
    tiles.push_back(MakeBandTestTile(48, 40, 1, 1, channel, 3, 40, 30,
                                     7.0 * channel, 11.0 * channel, 1.0F,
                                     10 + channel));
    tiles.back().op.tile_coord.x = channel;
  }
  const auto config =
      BandTestConfig(5, DataType::kUInt8, PlanarConfig::kSeparate);
  for (uint32_t band_rows : {1U, 7U, 64U}) {
    ExpectBandsMatchWholePaint(config, tiles, band_rows);
  }
}

}  // namespace runtime
}  // namespace fastslide
