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

/// @file tile_writer_uint16_test.cpp
/// @brief Tests for the 16-bit RGB integer-position copy path on `Canvas`.
///
/// These tests pin down the new behaviour added for MRXS 16-bit
/// fluorescence: the Canvas paints 16-bit RGB tiles via a templated copy
/// blit that tracks coverage so overlapping tiles follow first-writer-wins
/// semantics (matching the 8-bit `RgbBlitOffset` invariants).

#include "fastslide/runtime/tile_writer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

namespace fastslide {
namespace runtime {
namespace {

/// @brief Build a uint16 RGB tile filled with a constant (r,g,b).
std::vector<uint8_t> MakeRgb16Tile(uint32_t width, uint32_t height, uint16_t r,
                                   uint16_t g, uint16_t b) {
  std::vector<uint8_t> bytes(static_cast<size_t>(width) * height * 3 *
                             sizeof(uint16_t));
  uint16_t* px = reinterpret_cast<uint16_t*>(bytes.data());
  const size_t pixel_count = static_cast<size_t>(width) * height;
  for (size_t i = 0; i < pixel_count; ++i) {
    px[3 * i + 0] = r;
    px[3 * i + 1] = g;
    px[3 * i + 2] = b;
  }
  return bytes;
}

Canvas::Config MakeRgb16Config(uint32_t width, uint32_t height) {
  Canvas::Config cfg;
  cfg.dimensions = {width, height};
  cfg.channels = 3;
  cfg.data_type = DataType::kUInt16;
  cfg.planar_config = PlanarConfig::kContiguous;
  cfg.background = Canvas::BackgroundColor(0, 0, 0);
  cfg.enable_blending = false;
  return cfg;
}

uint16_t Sample(const Image& img, uint32_t x, uint32_t y, uint32_t channel) {
  const uint16_t* data = img.GetDataAs<uint16_t>();
  const size_t idx =
      (static_cast<size_t>(y) * img.GetWidth() + x) * 3 + channel;
  return data[idx];
}

TEST(TileWriterUInt16Test, ConstructRgb16CanvasPreservesBitDepth) {
  Canvas canvas(MakeRgb16Config(32, 16));
  EXPECT_EQ(canvas.GetDimensions()[0], 32U);
  EXPECT_EQ(canvas.GetDimensions()[1], 16U);
  EXPECT_EQ(canvas.GetChannels(), 3U);
}

TEST(TileWriterUInt16Test, IntegerPositionPaintWritesRgb16Pixels) {
  Canvas canvas(MakeRgb16Config(8, 8));
  const auto tile = MakeRgb16Tile(/*w=*/4, /*h=*/4, /*r=*/0xAAAA, /*g=*/0xBBBB,
                                  /*b=*/0xCCCC);

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0, 0, 4, 4};
  op.transform.dest = {2, 2, 4, 4};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = static_cast<uint32_t>(tile.size());

  ASSERT_TRUE(canvas
                  .PaintTile(op, tile, /*tile_w=*/4, /*tile_h=*/4,
                             /*tile_channels=*/3)
                  .ok());
  ASSERT_TRUE(canvas.Finalize().ok());
  auto result_or = canvas.GetOutput();
  ASSERT_TRUE(result_or.ok()) << result_or.status().ToString();
  const Image& img = result_or.value();

  // Inside the painted rectangle (2..6, 2..6) we should see the tile color.
  EXPECT_EQ(Sample(img, 2, 2, 0), 0xAAAA);
  EXPECT_EQ(Sample(img, 5, 5, 1), 0xBBBB);
  EXPECT_EQ(Sample(img, 4, 3, 2), 0xCCCC);
  // Outside it, the background remains 0 (we configured (0,0,0)).
  EXPECT_EQ(Sample(img, 0, 0, 0), 0);
  EXPECT_EQ(Sample(img, 7, 7, 2), 0);
}

TEST(TileWriterUInt16Test, OverlappingTilesAreFirstWriterWins) {
  Canvas canvas(MakeRgb16Config(8, 8));

  // First tile fills the full 8x8 canvas with red (0xFFFF, 0, 0).
  const auto first = MakeRgb16Tile(8, 8, 0xFFFF, 0, 0);
  core::TileReadOp first_op;
  first_op.level = 0;
  first_op.tile_coord = {0, 0};
  first_op.transform.source = {0, 0, 8, 8};
  first_op.transform.dest = {0, 0, 8, 8};
  first_op.source_id = 0;
  first_op.byte_offset = 0;
  first_op.byte_size = static_cast<uint32_t>(first.size());
  ASSERT_TRUE(canvas.PaintTile(first_op, first, 8, 8, 3).ok());

  // Second tile (green) overlaps the right half [4..8). Coverage from the
  // first paint should make the second a no-op for those pixels.
  const auto second = MakeRgb16Tile(4, 8, 0, 0xFFFF, 0);
  core::TileReadOp second_op;
  second_op.level = 0;
  second_op.tile_coord = {1, 0};
  second_op.transform.source = {0, 0, 4, 8};
  second_op.transform.dest = {4, 0, 4, 8};
  second_op.source_id = 0;
  second_op.byte_offset = 1;  // Distinct byte_offset so the cache key differs.
  second_op.byte_size = static_cast<uint32_t>(second.size());
  ASSERT_TRUE(canvas.PaintTile(second_op, second, 4, 8, 3).ok());

  ASSERT_TRUE(canvas.Finalize().ok());
  auto result_or = canvas.GetOutput();
  ASSERT_TRUE(result_or.ok()) << result_or.status().ToString();
  const Image& img = result_or.value();

  // The right half should still show red (first writer won).
  EXPECT_EQ(Sample(img, 0, 0, 0), 0xFFFF);
  EXPECT_EQ(Sample(img, 0, 0, 1), 0);
  EXPECT_EQ(Sample(img, 5, 0, 0), 0xFFFF);
  EXPECT_EQ(Sample(img, 5, 0, 1), 0);
  EXPECT_EQ(Sample(img, 7, 7, 0), 0xFFFF);
  EXPECT_EQ(Sample(img, 7, 7, 1), 0);
}

TEST(TileWriterUInt16Test, FractionalDestIsFlooredToInteger) {
  Canvas canvas(MakeRgb16Config(8, 8));
  const auto tile = MakeRgb16Tile(4, 4, 0x1234, 0x5678, 0x9ABC);

  core::TileReadOp op;
  op.level = 0;
  op.tile_coord = {0, 0};
  op.transform.source = {0.0, 0.0, 4, 4};
  // Fractional positions are floored; the 16-bit copy path is integer-only
  // by design (`blend_mode_16bit = integer_only_16` in the design plan).
  op.transform.dest = {1.7, 1.7, 4, 4};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = static_cast<uint32_t>(tile.size());

  ASSERT_TRUE(canvas.PaintTile(op, tile, 4, 4, 3).ok());
  ASSERT_TRUE(canvas.Finalize().ok());
  auto result_or = canvas.GetOutput();
  ASSERT_TRUE(result_or.ok()) << result_or.status().ToString();
  const Image& img = result_or.value();

  // Floor(1.7) = 1, so painted rectangle is [1..5, 1..5).
  EXPECT_EQ(Sample(img, 1, 1, 0), 0x1234);
  EXPECT_EQ(Sample(img, 4, 4, 2), 0x9ABC);
  EXPECT_EQ(Sample(img, 0, 0, 0), 0);
  EXPECT_EQ(Sample(img, 5, 5, 0), 0);
}

/// Regression test for the MRXS pixel inspector: reading a 1x1 region from
/// inside a much larger tile must paint the corresponding sample, not be
/// silently rejected because the destination origin is negative.
///
/// Before the fix, `PaintTileLocked` truncated `dst.x = bbox.min - read_x`
/// (typically negative) to `uint32_t`, which underflowed to a huge value
/// and then failed the canvas-bounds check, leaving the pixel inspector
/// always reading zeros for fluorescence slides.
TEST(TileWriterUInt16Test, OneByOneSubTileReadHitsTargetSampleInPlanar) {
  // 4-channel U16 separate-planes canvas, 1x1, mimicking the scalar probe
  // the FV viewer issues for the pixel inspector overlay.
  Canvas::Config cfg;
  cfg.dimensions = {1, 1};
  cfg.channels = 4;
  cfg.data_type = DataType::kUInt16;
  cfg.planar_config = PlanarConfig::kSeparate;
  cfg.background = Canvas::BackgroundColor(std::vector<double>{0, 0, 0, 0});
  cfg.enable_blending = false;
  cfg.force_spectral_image = true;
  Canvas canvas(cfg);

  // Build a 4x4 single-plane U16 tile. Picking a unique value at (1,2) so
  // we can verify the right tile coordinate is sampled by the clipper.
  std::vector<uint8_t> tile_bytes(4 * 4 * sizeof(uint16_t), 0);
  uint16_t* px = reinterpret_cast<uint16_t*>(tile_bytes.data());
  constexpr uint16_t kSentinel = 0xBEEF;
  px[2 * 4 + 1] = kSentinel;

  core::TileReadOp op;
  op.level = 0;
  // PaintTilePlanar uses tile_coord.x as the target channel index; channel
  // 2 (TRITC slot in a 4-filter slide) is a representative value.
  op.tile_coord = {2, 0};
  op.transform.source = {0.0, 0.0, 4, 4};
  // Negative dest, as produced by the MRXS plan builder when a 1x1 read
  // falls inside a larger tile (bbox.min - read_origin).
  op.transform.dest = {-1.0, -2.0, 4, 4};
  op.source_id = 0;
  op.byte_offset = 0;
  op.byte_size = static_cast<uint32_t>(tile_bytes.size());

  ASSERT_TRUE(canvas
                  .PaintTile(op, tile_bytes, /*tile_w=*/4, /*tile_h=*/4,
                             /*tile_channels=*/1)
                  .ok());
  ASSERT_TRUE(canvas.Finalize().ok());
  auto result_or = canvas.GetOutput();
  ASSERT_TRUE(result_or.ok()) << result_or.status().ToString();
  const Image& img = result_or.value();

  // 4 channels x 1 px = 4 u16 samples, in plane order [ch0, ch1, ch2, ch3].
  const uint16_t* data = img.GetDataAs<uint16_t>();
  EXPECT_EQ(data[0], 0);          // DAPI plane untouched.
  EXPECT_EQ(data[1], 0);          // FITC plane untouched.
  EXPECT_EQ(data[2], kSentinel);  // TRITC plane received tile (1, 2).
  EXPECT_EQ(data[3], 0);          // Cy5 plane untouched.
}

TEST(TileWriterUInt16Test, FillBackgroundWritesScaled16BitGray) {
  Canvas canvas(MakeRgb16Config(4, 2));
  ASSERT_TRUE(canvas.FillBackground(/*r=*/0x80, /*g=*/0x80, /*b=*/0x80).ok());
  ASSERT_TRUE(canvas.Finalize().ok());
  auto result_or = canvas.GetOutput();
  ASSERT_TRUE(result_or.ok()) << result_or.status().ToString();
  const Image& img = result_or.value();

  // 0x80 * 0x101 == 0x8080 -- replicating the 8-bit byte across both
  // halves of the 16-bit sample (lodepng convention).
  for (uint32_t y = 0; y < img.GetHeight(); ++y) {
    for (uint32_t x = 0; x < img.GetWidth(); ++x) {
      EXPECT_EQ(Sample(img, x, y, 0), 0x8080);
      EXPECT_EQ(Sample(img, x, y, 1), 0x8080);
      EXPECT_EQ(Sample(img, x, y, 2), 0x8080);
    }
  }
}

}  // namespace
}  // namespace runtime
}  // namespace fastslide
