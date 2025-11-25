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

#include "fastslide/runtime/tile_writer/blended/accumulate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "fastslide/runtime/tile_writer/blended/srgb_linear.h"
#include "hwy/aligned_allocator.h"

namespace fastslide {
namespace runtime {

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Convert sRGB uint8 value to linear float [0,1] using actual codebase
/// function This ensures round-trip consistency with the actual implementation
float Srgb8ToLinear(uint8_t srgb) {
  // Create a 1x1 tile with the single sRGB value in RGB format
  uint8_t srgb_data[3] = {srgb, srgb, srgb};
  hwy::AlignedVector<float> linear_data(3 + 16);  // 3 channels + padding

  // Use the actual conversion function from the codebase
  ConvertSrgb8ToLinearPlanar(srgb_data, 1, 1, linear_data.data());

  // Return R channel (all channels are the same for grayscale)
  return linear_data[0];
}

/// @brief Convert linear float [0,1] to sRGB uint8
uint8_t LinearToSrgb8(float linear) {
  return LinearToSrgb8Fast(linear);
}

/// @brief Create test linear RGB data in planar format
/// Returns vector with R plane, G plane, B plane sequentially
hwy::AlignedVector<float> CreateTestLinearPlanar(int width, int height) {
  const size_t plane_size = static_cast<size_t>(width) * height;
  hwy::AlignedVector<float> data(plane_size * 3);

  float* r_plane = data.data();
  float* g_plane = data.data() + plane_size;
  float* b_plane = data.data() + 2 * plane_size;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t idx = y * width + x;

      // Create gradient pattern in linear space
      r_plane[idx] = static_cast<float>(x) / width;
      g_plane[idx] = static_cast<float>(y) / height;
      b_plane[idx] = static_cast<float>(x + y) / (width + height);
    }
  }

  return data;
}

/// @brief Create uniform color in linear RGB planar format
hwy::AlignedVector<float> CreateUniformLinearPlanar(int width, int height,
                                                    float r, float g, float b) {
  const size_t plane_size = static_cast<size_t>(width) * height;
  hwy::AlignedVector<float> data(plane_size * 3);

  float* r_plane = data.data();
  float* g_plane = data.data() + plane_size;
  float* b_plane = data.data() + 2 * plane_size;

  std::fill_n(r_plane, plane_size, r);
  std::fill_n(g_plane, plane_size, g);
  std::fill_n(b_plane, plane_size, b);

  return data;
}

// ============================================================================
// AccumulateLinearTile Tests
// ============================================================================

TEST(AccumulateLinearTileTest, SingleTileFullCoverage) {
  const int img_w = 64;
  const int img_h = 64;
  const int tile_w = 64;
  const int tile_h = 64;
  const float weight = 1.0f;

  // Create input tile
  auto tile_data = CreateUniformLinearPlanar(tile_w, tile_h, 0.5f, 0.3f, 0.7f);

  // Create output accumulators
  const size_t pixel_count = img_w * img_h;
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 0.0f);

  absl::Mutex mutex;

  // Accumulate tile at (0, 0)
  AccumulateLinearTile(tile_data.data(), tile_w, tile_h, 0, 0, weight,
                       acc_r.data(), acc_g.data(), acc_b.data(),
                       weight_sum.data(), img_w, img_h, mutex);

  // Verify accumulation
  for (size_t i = 0; i < pixel_count; ++i) {
    EXPECT_NEAR(acc_r[i], 0.5f * weight, 1e-6f) << "Pixel " << i;
    EXPECT_NEAR(acc_g[i], 0.3f * weight, 1e-6f) << "Pixel " << i;
    EXPECT_NEAR(acc_b[i], 0.7f * weight, 1e-6f) << "Pixel " << i;
    EXPECT_NEAR(weight_sum[i], weight, 1e-6f) << "Pixel " << i;
  }
}

TEST(AccumulateLinearTileTest, PartialTilePlacement) {
  const int img_w = 128;
  const int img_h = 128;
  const int tile_w = 64;
  const int tile_h = 64;
  const float weight = 1.0f;

  // Create input tile
  auto tile_data = CreateUniformLinearPlanar(tile_w, tile_h, 0.8f, 0.4f, 0.2f);

  // Create output accumulators
  const size_t pixel_count = img_w * img_h;
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 0.0f);

  absl::Mutex mutex;

  // Accumulate tile at (32, 32) - should be in center
  AccumulateLinearTile(tile_data.data(), tile_w, tile_h, 32, 32, weight,
                       acc_r.data(), acc_g.data(), acc_b.data(),
                       weight_sum.data(), img_w, img_h, mutex);

  // Verify only the tile region was accumulated
  for (int y = 0; y < img_h; ++y) {
    for (int x = 0; x < img_w; ++x) {
      const size_t idx = y * img_w + x;

      if (x >= 32 && x < 96 && y >= 32 && y < 96) {
        // Inside tile region
        EXPECT_NEAR(acc_r[idx], 0.8f, 1e-6f)
            << "Pixel (" << x << "," << y << ")";
        EXPECT_NEAR(acc_g[idx], 0.4f, 1e-6f)
            << "Pixel (" << x << "," << y << ")";
        EXPECT_NEAR(acc_b[idx], 0.2f, 1e-6f)
            << "Pixel (" << x << "," << y << ")";
        EXPECT_NEAR(weight_sum[idx], 1.0f, 1e-6f)
            << "Pixel (" << x << "," << y << ")";
      } else {
        // Outside tile region
        EXPECT_FLOAT_EQ(acc_r[idx], 0.0f) << "Pixel (" << x << "," << y << ")";
        EXPECT_FLOAT_EQ(acc_g[idx], 0.0f) << "Pixel (" << x << "," << y << ")";
        EXPECT_FLOAT_EQ(acc_b[idx], 0.0f) << "Pixel (" << x << "," << y << ")";
        EXPECT_FLOAT_EQ(weight_sum[idx], 0.0f)
            << "Pixel (" << x << "," << y << ")";
      }
    }
  }
}

TEST(AccumulateLinearTileTest, OverlappingTilesWithWeights) {
  const int img_w = 100;
  const int img_h = 100;
  const int tile_w = 60;
  const int tile_h = 60;

  // Create two different tiles
  auto tile1 =
      CreateUniformLinearPlanar(tile_w, tile_h, 1.0f, 0.0f, 0.0f);  // Red
  auto tile2 =
      CreateUniformLinearPlanar(tile_w, tile_h, 0.0f, 1.0f, 0.0f);  // Green

  // Create output accumulators
  const size_t pixel_count = img_w * img_h;
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 0.0f);

  absl::Mutex mutex;

  // Accumulate first tile at (0, 0) with weight 0.6
  AccumulateLinearTile(tile1.data(), tile_w, tile_h, 0, 0, 0.6f, acc_r.data(),
                       acc_g.data(), acc_b.data(), weight_sum.data(), img_w,
                       img_h, mutex);

  // Accumulate second tile at (40, 40) with weight 0.4
  AccumulateLinearTile(tile2.data(), tile_w, tile_h, 40, 40, 0.4f, acc_r.data(),
                       acc_g.data(), acc_b.data(), weight_sum.data(), img_w,
                       img_h, mutex);

  // Verify overlap region (40-60, 40-60) should have both contributions
  for (int y = 40; y < 60; ++y) {
    for (int x = 40; x < 60; ++x) {
      const size_t idx = y * img_w + x;
      EXPECT_NEAR(acc_r[idx], 0.6f, 1e-6f) << "Pixel (" << x << "," << y << ")";
      EXPECT_NEAR(acc_g[idx], 0.4f, 1e-6f) << "Pixel (" << x << "," << y << ")";
      EXPECT_NEAR(weight_sum[idx], 1.0f, 1e-6f)
          << "Pixel (" << x << "," << y << ")";
    }
  }
}

TEST(AccumulateLinearTileTest, ClippingAtImageBoundary) {
  const int img_w = 64;
  const int img_h = 64;
  const int tile_w = 80;  // Larger than image
  const int tile_h = 80;

  auto tile_data = CreateUniformLinearPlanar(tile_w, tile_h, 0.5f, 0.5f, 0.5f);

  const size_t pixel_count = img_w * img_h;
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 0.0f);

  absl::Mutex mutex;

  // Tile placed at (-10, -10) - should be clipped
  AccumulateLinearTile(tile_data.data(), tile_w, tile_h, -10, -10, 1.0f,
                       acc_r.data(), acc_g.data(), acc_b.data(),
                       weight_sum.data(), img_w, img_h, mutex);

  // Verify all pixels within image bounds were accumulated
  for (size_t i = 0; i < pixel_count; ++i) {
    EXPECT_NEAR(acc_r[i], 0.5f, 1e-6f);
    EXPECT_NEAR(acc_g[i], 0.5f, 1e-6f);
    EXPECT_NEAR(acc_b[i], 0.5f, 1e-6f);
    EXPECT_NEAR(weight_sum[i], 1.0f, 1e-6f);
  }
}

// ============================================================================
// FinalizeLinearToSrgb8 Tests
// ============================================================================

TEST(FinalizeLinearToSrgb8Test, UniformColor) {
  const int img_w = 64;
  const int img_h = 64;
  const size_t pixel_count = img_w * img_h;

  // Create uniform linear color accumulators
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 0.5f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 0.3f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 0.8f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 1.0f);

  // Output buffer
  std::vector<uint8_t> output(pixel_count * 3);

  // Finalize
  FinalizeLinearToSrgb8(acc_r.data(), acc_g.data(), acc_b.data(),
                        weight_sum.data(), img_w, img_h, output.data());

  // Verify all pixels have correct sRGB values
  // Use NEAR with tolerance of 1 due to quantization in sRGB ↔ linear
  // round-trip
  const uint8_t expected_r = LinearToSrgb8(0.5f);
  const uint8_t expected_g = LinearToSrgb8(0.3f);
  const uint8_t expected_b = LinearToSrgb8(0.8f);

  for (size_t i = 0; i < pixel_count; ++i) {
    EXPECT_NEAR(output[i * 3 + 0], expected_r, 1) << "Pixel " << i;
    EXPECT_NEAR(output[i * 3 + 1], expected_g, 1) << "Pixel " << i;
    EXPECT_NEAR(output[i * 3 + 2], expected_b, 1) << "Pixel " << i;
  }
}

TEST(FinalizeLinearToSrgb8Test, WithWeightedNormalization) {
  const int img_w = 32;
  const int img_h = 32;
  const size_t pixel_count = img_w * img_h;

  // Create accumulators with weight = 2.0
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding,
                                  1.0f);  // 1.0 / 2.0 = 0.5
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding,
                                  0.6f);  // 0.6 / 2.0 = 0.3
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding,
                                  1.6f);  // 1.6 / 2.0 = 0.8
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 2.0f);

  std::vector<uint8_t> output(pixel_count * 3);

  FinalizeLinearToSrgb8(acc_r.data(), acc_g.data(), acc_b.data(),
                        weight_sum.data(), img_w, img_h, output.data());

  // After normalization, should match uniform color test
  // Use NEAR with tolerance of 1 due to quantization in sRGB ↔ linear
  // round-trip
  const uint8_t expected_r = LinearToSrgb8(0.5f);
  const uint8_t expected_g = LinearToSrgb8(0.3f);
  const uint8_t expected_b = LinearToSrgb8(0.8f);

  for (size_t i = 0; i < pixel_count; ++i) {
    EXPECT_NEAR(output[i * 3 + 0], expected_r, 1) << "Pixel " << i;
    EXPECT_NEAR(output[i * 3 + 1], expected_g, 1) << "Pixel " << i;
    EXPECT_NEAR(output[i * 3 + 2], expected_b, 1) << "Pixel " << i;
  }
}

TEST(FinalizeLinearToSrgb8Test, ZeroWeight) {
  const int img_w = 16;
  const int img_h = 16;
  const size_t pixel_count = img_w * img_h;

  // Create accumulators with zero weight (should result in black)
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 1.0f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 1.0f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 1.0f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 0.0f);

  std::vector<uint8_t> output(pixel_count * 3);

  FinalizeLinearToSrgb8(acc_r.data(), acc_g.data(), acc_b.data(),
                        weight_sum.data(), img_w, img_h, output.data());

  // Zero weight should result in black (0, 0, 0)
  for (size_t i = 0; i < pixel_count; ++i) {
    EXPECT_EQ(output[i * 3 + 0], 0) << "Pixel " << i;
    EXPECT_EQ(output[i * 3 + 1], 0) << "Pixel " << i;
    EXPECT_EQ(output[i * 3 + 2], 0) << "Pixel " << i;
  }
}

TEST(FinalizeLinearToSrgb8Test, CacheBlockingLargeImage) {
  // Test with image larger than cache block size (64x64)
  const int img_w = 256;
  const int img_h = 256;
  const size_t pixel_count = img_w * img_h;

  // Create gradient pattern
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 1.0f);

  for (int y = 0; y < img_h; ++y) {
    for (int x = 0; x < img_w; ++x) {
      const size_t idx = y * img_w + x;
      acc_r[idx] = static_cast<float>(x) / img_w;
      acc_g[idx] = static_cast<float>(y) / img_h;
      acc_b[idx] = 0.5f;
    }
  }

  std::vector<uint8_t> output(pixel_count * 3);

  FinalizeLinearToSrgb8(acc_r.data(), acc_g.data(), acc_b.data(),
                        weight_sum.data(), img_w, img_h, output.data());

  // Verify gradient is preserved through sRGB conversion
  // Use NEAR with tolerance of 1 due to quantization in sRGB ↔ linear
  // round-trip
  for (int y = 0; y < img_h; ++y) {
    for (int x = 0; x < img_w; ++x) {
      const size_t idx = y * img_w + x;
      const float linear_r = static_cast<float>(x) / img_w;
      const float linear_g = static_cast<float>(y) / img_h;

      const uint8_t expected_r = LinearToSrgb8(linear_r);
      const uint8_t expected_g = LinearToSrgb8(linear_g);
      const uint8_t expected_b = LinearToSrgb8(0.5f);

      EXPECT_NEAR(output[idx * 3 + 0], expected_r, 1)
          << "Pixel (" << x << "," << y << ")";
      EXPECT_NEAR(output[idx * 3 + 1], expected_g, 1)
          << "Pixel (" << x << "," << y << ")";
      EXPECT_NEAR(output[idx * 3 + 2], expected_b, 1)
          << "Pixel (" << x << "," << y << ")";
    }
  }
}

TEST(FinalizeLinearToSrgb8Test, NonStandardDimensions) {
  // Test with dimensions not aligned to SIMD width or cache block size
  const int img_w = 63;  // Not power of 2
  const int img_h = 127;
  const size_t pixel_count = img_w * img_h;

  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 0.25f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 0.50f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 0.75f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 1.0f);

  std::vector<uint8_t> output(pixel_count * 3);

  FinalizeLinearToSrgb8(acc_r.data(), acc_g.data(), acc_b.data(),
                        weight_sum.data(), img_w, img_h, output.data());

  // Use NEAR with tolerance of 1 due to quantization in sRGB ↔ linear
  // round-trip
  const uint8_t expected_r = LinearToSrgb8(0.25f);
  const uint8_t expected_g = LinearToSrgb8(0.50f);
  const uint8_t expected_b = LinearToSrgb8(0.75f);

  for (size_t i = 0; i < pixel_count; ++i) {
    EXPECT_NEAR(output[i * 3 + 0], expected_r, 1) << "Pixel " << i;
    EXPECT_NEAR(output[i * 3 + 1], expected_g, 1) << "Pixel " << i;
    EXPECT_NEAR(output[i * 3 + 2], expected_b, 1) << "Pixel " << i;
  }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(AccumulateFinalizeIntegrationTest, EndToEndPipeline) {
  // Test complete accumulate -> finalize pipeline
  const int img_w = 128;
  const int img_h = 128;
  const int tile_w = 64;
  const int tile_h = 64;

  // Create two tiles with known colors
  auto tile1 = CreateUniformLinearPlanar(tile_w, tile_h, 0.4f, 0.0f, 0.0f);
  auto tile2 = CreateUniformLinearPlanar(tile_w, tile_h, 0.0f, 0.6f, 0.0f);

  const size_t pixel_count = img_w * img_h;
  constexpr size_t kPadding = 16;
  hwy::AlignedVector<float> acc_r(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_g(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> acc_b(pixel_count + kPadding, 0.0f);
  hwy::AlignedVector<float> weight_sum(pixel_count + kPadding, 0.0f);

  absl::Mutex mutex;

  // Accumulate tiles in 2x2 grid pattern
  AccumulateLinearTile(tile1.data(), tile_w, tile_h, 0, 0, 1.0f, acc_r.data(),
                       acc_g.data(), acc_b.data(), weight_sum.data(), img_w,
                       img_h, mutex);

  AccumulateLinearTile(tile2.data(), tile_w, tile_h, 64, 0, 1.0f, acc_r.data(),
                       acc_g.data(), acc_b.data(), weight_sum.data(), img_w,
                       img_h, mutex);

  AccumulateLinearTile(tile2.data(), tile_w, tile_h, 0, 64, 1.0f, acc_r.data(),
                       acc_g.data(), acc_b.data(), weight_sum.data(), img_w,
                       img_h, mutex);

  AccumulateLinearTile(tile1.data(), tile_w, tile_h, 64, 64, 1.0f, acc_r.data(),
                       acc_g.data(), acc_b.data(), weight_sum.data(), img_w,
                       img_h, mutex);

  // Finalize
  std::vector<uint8_t> output(pixel_count * 3);
  FinalizeLinearToSrgb8(acc_r.data(), acc_g.data(), acc_b.data(),
                        weight_sum.data(), img_w, img_h, output.data());

  // Verify quadrants
  const uint8_t red_expected = LinearToSrgb8(0.4f);
  const uint8_t green_expected = LinearToSrgb8(0.6f);

  // Top-left: red
  const size_t idx_tl = (32 * img_w + 32) * 3;
  EXPECT_EQ(output[idx_tl + 0], red_expected);
  EXPECT_EQ(output[idx_tl + 1], 0);
  EXPECT_EQ(output[idx_tl + 2], 0);

  // Top-right: green
  const size_t idx_tr = (32 * img_w + 96) * 3;
  EXPECT_EQ(output[idx_tr + 0], 0);
  EXPECT_EQ(output[idx_tr + 1], green_expected);
  EXPECT_EQ(output[idx_tr + 2], 0);

  // Bottom-left: green
  const size_t idx_bl = (96 * img_w + 32) * 3;
  EXPECT_EQ(output[idx_bl + 0], 0);
  EXPECT_EQ(output[idx_bl + 1], green_expected);
  EXPECT_EQ(output[idx_bl + 2], 0);

  // Bottom-right: red
  const size_t idx_br = (96 * img_w + 96) * 3;
  EXPECT_EQ(output[idx_br + 0], red_expected);
  EXPECT_EQ(output[idx_br + 1], 0);
  EXPECT_EQ(output[idx_br + 2], 0);
}

}  // namespace runtime
}  // namespace fastslide
