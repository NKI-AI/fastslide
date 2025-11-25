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

#include "fastslide/image.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace fastslide {

class ImageTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {}
};

// Test basic image creation and properties
TEST_F(ImageTest, BasicImageCreation) {
  ImageDimensions dims{100, 50};

  // Test RGB image creation
  Image rgb_image(dims, ImageFormat::kRGB, DataType::kUInt8);

  EXPECT_EQ(rgb_image.GetWidth(), 100);
  EXPECT_EQ(rgb_image.GetHeight(), 50);
  EXPECT_EQ(rgb_image.GetChannels(), 3);
  EXPECT_EQ(rgb_image.GetFormat(), ImageFormat::kRGB);
  EXPECT_EQ(rgb_image.GetDataType(), DataType::kUInt8);
  EXPECT_EQ(rgb_image.GetBytesPerSample(), 1);
  EXPECT_FALSE(rgb_image.Empty());
  EXPECT_EQ(rgb_image.SizeBytes(),
            100 * 50 * 3 * 1);  // width * height * channels * bytes_per_sample
}

// Test different image formats
TEST_F(ImageTest, DifferentFormats) {
  ImageDimensions dims{10, 10};

  // Grayscale
  Image gray_image(dims, ImageFormat::kGray, DataType::kUInt8);
  EXPECT_EQ(gray_image.GetChannels(), 1);
  EXPECT_EQ(gray_image.SizeBytes(), 10 * 10 * 1 * 1);

  // RGBA
  Image rgba_image(dims, ImageFormat::kRGBA, DataType::kUInt8);
  EXPECT_EQ(rgba_image.GetChannels(), 4);
  EXPECT_EQ(rgba_image.SizeBytes(), 10 * 10 * 4 * 1);

  // Spectral (5 channels)
  Image spectral_image(dims, 5, DataType::kUInt8);
  EXPECT_EQ(spectral_image.GetChannels(), 5);
  EXPECT_EQ(spectral_image.GetFormat(), ImageFormat::kSpectral);
  EXPECT_EQ(spectral_image.SizeBytes(), 10 * 10 * 5 * 1);
}

// Test different data types
TEST_F(ImageTest, DifferentDataTypes) {
  ImageDimensions dims{10, 10};

  // 16-bit unsigned
  Image uint16_image(dims, ImageFormat::kRGB, DataType::kUInt16);
  EXPECT_EQ(uint16_image.GetBytesPerSample(), 2);
  EXPECT_EQ(uint16_image.SizeBytes(), 10 * 10 * 3 * 2);

  // 32-bit float
  Image float32_image(dims, ImageFormat::kRGB, DataType::kFloat32);
  EXPECT_EQ(float32_image.GetBytesPerSample(), 4);
  EXPECT_EQ(float32_image.SizeBytes(), 10 * 10 * 3 * 4);

  // 64-bit float
  Image float64_image(dims, ImageFormat::kGray, DataType::kFloat64);
  EXPECT_EQ(float64_image.GetBytesPerSample(), 8);
  EXPECT_EQ(float64_image.SizeBytes(), 10 * 10 * 1 * 8);
}

// Test pixel access
TEST_F(ImageTest, PixelAccess) {
  ImageDimensions dims{5, 5};
  Image image(dims, ImageFormat::kRGB, DataType::kUInt8);

  // Set pixel values
  image.At<uint8_t>(2, 3, 0) = 255;  // Red channel
  image.At<uint8_t>(2, 3, 1) = 128;  // Green channel
  image.At<uint8_t>(2, 3, 2) = 64;   // Blue channel

  // Read pixel values
  EXPECT_EQ(image.At<uint8_t>(2, 3, 0), 255);
  EXPECT_EQ(image.At<uint8_t>(2, 3, 1), 128);
  EXPECT_EQ(image.At<uint8_t>(2, 3, 2), 64);
}

// Test pixel access with different data types
TEST_F(ImageTest, PixelAccessDifferentTypes) {
  ImageDimensions dims{3, 3};

  // Float32 image
  Image float_image(dims, ImageFormat::kGray, DataType::kFloat32);
  float_image.At<float>(1, 1, 0) = 3.14159f;
  EXPECT_FLOAT_EQ(float_image.At<float>(1, 1, 0), 3.14159f);

  // Uint16 image
  Image uint16_image(dims, ImageFormat::kRGB, DataType::kUInt16);
  uint16_image.At<uint16_t>(0, 0, 1) = 65535;
  EXPECT_EQ(uint16_image.At<uint16_t>(0, 0, 1), 65535);
}

// Test bounds checking
TEST_F(ImageTest, BoundsChecking) {
  ImageDimensions dims{5, 5};
  Image image(dims, ImageFormat::kRGB, DataType::kUInt8);

  // Valid access should work
  EXPECT_NO_THROW(image.At<uint8_t>(4, 4, 2));

  // Out of bounds access should throw
  EXPECT_THROW(image.At<uint8_t>(5, 0, 0),
               std::out_of_range);  // x out of bounds
  EXPECT_THROW(image.At<uint8_t>(0, 5, 0),
               std::out_of_range);  // y out of bounds
  EXPECT_THROW(image.At<uint8_t>(0, 0, 3),
               std::out_of_range);  // channel out of bounds
}

// Test type safety
TEST_F(ImageTest, TypeSafety) {
  ImageDimensions dims{3, 3};
  Image uint8_image(dims, ImageFormat::kRGB, DataType::kUInt8);
  Image float32_image(dims, ImageFormat::kRGB, DataType::kFloat32);

  // Should work with correct types
  EXPECT_NO_THROW(uint8_image.GetDataAs<uint8_t>());
  EXPECT_NO_THROW(float32_image.GetDataAs<float>());

  // Should throw with incorrect types
  EXPECT_THROW(uint8_image.GetDataAs<float>(), std::runtime_error);
  EXPECT_THROW(float32_image.GetDataAs<uint8_t>(), std::runtime_error);
}

// Test image conversions
TEST_F(ImageTest, ImageConversions) {
  ImageDimensions dims{10, 10};

  // Create RGB image and convert to grayscale
  Image rgb_image(dims, ImageFormat::kRGB, DataType::kUInt8);

  // Set some pixel values
  rgb_image.At<uint8_t>(5, 5, 0) = 100;  // R
  rgb_image.At<uint8_t>(5, 5, 1) = 150;  // G
  rgb_image.At<uint8_t>(5, 5, 2) = 200;  // B

  auto gray_image = rgb_image.ToGrayscale();
  ASSERT_NE(gray_image, nullptr);
  EXPECT_EQ(gray_image->GetFormat(), ImageFormat::kGray);
  EXPECT_EQ(gray_image->GetChannels(), 1);
  EXPECT_EQ(gray_image->GetDimensions(), dims);

  // Test the conversion result (luminance formula: 0.299*R + 0.587*G + 0.114*B)
  // We use the approximation 0.25*R + 0.5*G + 0.125*B
  uint8_t expected_gray =
      static_cast<uint8_t>((250 * 100 + 500 * 150 + 125 * 200) / 1000);
  EXPECT_EQ(gray_image->At<uint8_t>(5, 5, 0), expected_gray);
}

// Test channel extraction
TEST_F(ImageTest, ChannelExtraction) {
  ImageDimensions dims{5, 5};

  // Create 5-channel spectral image
  Image spectral_image(dims, 5, DataType::kUInt8);

  // Set values in different channels
  for (uint32_t ch = 0; ch < 5; ++ch) {
    spectral_image.At<uint8_t>(2, 2, ch) = static_cast<uint8_t>(ch * 50);
  }

  // Extract channels 1, 3, and 4 to create a 3-channel RGB image
  std::vector<uint32_t> channel_indices = {1, 3, 4};
  auto extracted_image = spectral_image.ExtractChannels(channel_indices);

  ASSERT_NE(extracted_image, nullptr);
  EXPECT_EQ(extracted_image->GetChannels(), 3);
  EXPECT_EQ(extracted_image->GetFormat(), ImageFormat::kRGB);

  // Check extracted values
  EXPECT_EQ(extracted_image->At<uint8_t>(2, 2, 0), 50);   // Channel 1 -> Red
  EXPECT_EQ(extracted_image->At<uint8_t>(2, 2, 1), 150);  // Channel 3 -> Green
  EXPECT_EQ(extracted_image->At<uint8_t>(2, 2, 2), 200);  // Channel 4 -> Blue
}

// Test factory functions
TEST_F(ImageTest, FactoryFunctions) {
  ImageDimensions dims{20, 15};

  auto rgb_image = CreateRGBImage(dims);
  EXPECT_EQ(rgb_image->GetFormat(), ImageFormat::kRGB);
  EXPECT_EQ(rgb_image->GetDataType(), DataType::kUInt8);

  auto rgba_image = CreateRGBAImage(dims, DataType::kUInt16);
  EXPECT_EQ(rgba_image->GetFormat(), ImageFormat::kRGBA);
  EXPECT_EQ(rgba_image->GetDataType(), DataType::kUInt16);

  auto gray_image = CreateGrayscaleImage(dims, DataType::kFloat32);
  EXPECT_EQ(gray_image->GetFormat(), ImageFormat::kGray);
  EXPECT_EQ(gray_image->GetDataType(), DataType::kFloat32);

  auto spectral_image = CreateSpectralImage(dims, 7, DataType::kInt16);
  EXPECT_EQ(spectral_image->GetFormat(), ImageFormat::kSpectral);
  EXPECT_EQ(spectral_image->GetChannels(), 7);
  EXPECT_EQ(spectral_image->GetDataType(), DataType::kInt16);
}

// Test empty image handling
TEST_F(ImageTest, EmptyImageHandling) {
  Image empty_image;
  EXPECT_TRUE(empty_image.Empty());
  EXPECT_EQ(empty_image.SizeBytes(), 0);

  // Empty image conversions should return nullptr
  auto empty_gray = empty_image.ToGrayscale();
  EXPECT_EQ(empty_gray, nullptr);

  auto empty_rgb = empty_image.ToRGB();
  EXPECT_EQ(empty_rgb, nullptr);
}

// Test description string
TEST_F(ImageTest, DescriptionString) {
  ImageDimensions dims{100, 200};

  Image rgb_image(dims, ImageFormat::kRGB, DataType::kUInt8);
  std::string desc = rgb_image.GetDescription();

  EXPECT_NE(desc.find("RGB"), std::string::npos);
  EXPECT_NE(desc.find("100x200x3"), std::string::npos);
  EXPECT_NE(desc.find("uint8"), std::string::npos);
}

// Test that spectral images use interleaved format by default
TEST_F(ImageTest, SpectralImagePlanarConfig) {
  ImageDimensions dims{10, 10};
  const uint32_t num_channels = 5;

  // Create spectral image using default constructor
  Image spectral_image(dims, num_channels, DataType::kFloat32);

  // Verify it uses interleaved format (recommended for spectral images)
  EXPECT_EQ(spectral_image.GetPlanarConfig(), PlanarConfig::kContiguous);
  EXPECT_EQ(spectral_image.GetFormat(), ImageFormat::kSpectral);
  EXPECT_EQ(spectral_image.GetChannels(), num_channels);

  // Test factory function also defaults to interleaved
  auto factory_spectral =
      CreateSpectralImage(dims, num_channels, DataType::kFloat32);
  EXPECT_EQ(factory_spectral->GetPlanarConfig(), PlanarConfig::kContiguous);

  // Verify pixel access works correctly with interleaved layout
  for (uint32_t ch = 0; ch < num_channels; ++ch) {
    spectral_image.At<float>(5, 5, ch) = static_cast<float>(ch + 1) * 10.0f;
  }

  // Verify the values were set correctly
  for (uint32_t ch = 0; ch < num_channels; ++ch) {
    EXPECT_FLOAT_EQ(spectral_image.At<float>(5, 5, ch),
                    static_cast<float>(ch + 1) * 10.0f);
  }
}

// Test hyperspectral use case
TEST_F(ImageTest, HyperspectralUseCase) {
  ImageDimensions dims{256, 256};
  const uint32_t num_bands = 224;  // Typical hyperspectral band count

  // Create hyperspectral image with float32 data
  Image hyperspectral(dims, num_bands, DataType::kFloat32);

  EXPECT_EQ(hyperspectral.GetFormat(), ImageFormat::kSpectral);
  EXPECT_EQ(hyperspectral.GetChannels(), num_bands);
  EXPECT_EQ(hyperspectral.GetDataType(), DataType::kFloat32);

  // Set spectral signature for a pixel
  for (uint32_t band = 0; band < num_bands; ++band) {
    float reflectance = std::sin(static_cast<float>(band) * 0.1f) * 0.5f + 0.5f;
    hyperspectral.At<float>(128, 128, band) = reflectance;
  }

  // Verify the data was set correctly
  EXPECT_FLOAT_EQ(hyperspectral.At<float>(128, 128, 0), 0.5f);
  EXPECT_GT(hyperspectral.At<float>(128, 128, 10),
            0.5f);  // Should be > 0.5 due to sin function
}

// Performance test for large images
TEST_F(ImageTest, LargeImagePerformance) {
  ImageDimensions dims{1024, 1024};

  // Create large RGB image
  auto start = std::chrono::high_resolution_clock::now();
  Image large_image(dims, ImageFormat::kRGB, DataType::kUInt8);
  auto end = std::chrono::high_resolution_clock::now();

  auto creation_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // Should create quickly (less than 100ms)
  EXPECT_LT(creation_time.count(), 100);

  // Verify size
  EXPECT_EQ(large_image.SizeBytes(), 1024 * 1024 * 3);
  EXPECT_FALSE(large_image.Empty());
}

}  // namespace fastslide
