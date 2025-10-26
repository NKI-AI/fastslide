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

#include <tiffio.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/image.h"
#include "fastslide/utilities/tiff/tiff_file.h"
#include "fastslide/utilities/tiff/tiff_pool.h"

namespace fastslide {

constexpr uint32_t kTestImageWidth = 256;
constexpr uint32_t kTestImageHeight = 256;
constexpr uint32_t kTestTileWidth = 64;
constexpr uint32_t kTestTileHeight = 64;

class TiffFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create test directory
    test_dir_ = std::filesystem::temp_directory_path() / "tiff_file_test";
    std::filesystem::create_directories(test_dir_);

    // Create test files
    CreateTestTiffFile();
    CreateTestStripTiffFile();
    CreateMultiDirectoryTiffFile();
    CreateInvalidTiffFile();
  }

  void TearDown() override {
    // Clean up test directory
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  /// @brief Create a basic tiled TIFF file for testing
  void CreateTestTiffFile() {
    test_tiff_path_ = test_dir_ / "test.tiff";

    TIFF* tif = TIFFOpen(test_tiff_path_.string().c_str(), "w");
    ASSERT_NE(tif, nullptr);

    // Set basic tags
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, kTestImageWidth);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, kTestImageHeight);
    TIFFSetField(tif, TIFFTAG_TILEWIDTH, kTestTileWidth);
    TIFFSetField(tif, TIFFTAG_TILELENGTH, kTestTileHeight);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, "Test TIFF file");
    TIFFSetField(tif, TIFFTAG_SOFTWARE, "TiffFileTest");
    float x_res = 72.0F;
    float y_res = 72.0F;
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, x_res);
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, y_res);
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);

    // Write tile data
    const size_t tile_size = kTestTileWidth * kTestTileHeight * 3;
    std::vector<uint8_t> tile_data(tile_size);

    // Fill with gradient pattern
    for (uint32_t y = 0; y < kTestTileHeight; ++y) {
      for (uint32_t x = 0; x < kTestTileWidth; ++x) {
        size_t idx = (y * kTestTileWidth + x) * 3;
        tile_data[idx] = static_cast<uint8_t>(x * 255 / kTestTileWidth);  // R
        tile_data[idx + 1] =
            static_cast<uint8_t>(y * 255 / kTestTileHeight);  // G
        tile_data[idx + 2] = 128;                             // B
      }
    }

    // Write all tiles
    for (uint32_t ty = 0; ty < kTestImageHeight; ty += kTestTileHeight) {
      for (uint32_t tx = 0; tx < kTestImageWidth; tx += kTestTileWidth) {
        TIFFWriteTile(tif, tile_data.data(), tx, ty, 0, 0);
      }
    }

    TIFFClose(tif);
  }

  /// @brief Create a strip-based TIFF file for testing
  void CreateTestStripTiffFile() {
    test_strip_tiff_path_ = test_dir_ / "test_strip.tiff";

    TIFF* tif = TIFFOpen(test_strip_tiff_path_.string().c_str(), "w");
    ASSERT_NE(tif, nullptr);

    // Set basic tags for strip-based image
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, kTestImageWidth);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, kTestImageHeight);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);

    // Write scanline data
    std::vector<uint8_t> scanline_data(kTestImageWidth);
    for (uint32_t y = 0; y < kTestImageHeight; ++y) {
      for (uint32_t x = 0; x < kTestImageWidth; ++x) {
        scanline_data[x] = static_cast<uint8_t>((x + y) % 256);
      }
      TIFFWriteScanline(tif, scanline_data.data(), y, 0);
    }

    TIFFClose(tif);
  }

  /// @brief Create a multi-directory TIFF file for testing
  void CreateMultiDirectoryTiffFile() {
    test_multi_dir_tiff_path_ = test_dir_ / "test_multi_dir.tiff";

    TIFF* tif = TIFFOpen(test_multi_dir_tiff_path_.string().c_str(), "w");
    ASSERT_NE(tif, nullptr);

    // Create 3 directories with different sizes
    const std::vector<uint32_t> widths = {256, 128, 64};
    const std::vector<uint32_t> heights = {256, 128, 64};

    for (size_t i = 0; i < widths.size(); ++i) {
      // Set directory tags
      TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, widths[i]);
      TIFFSetField(tif, TIFFTAG_IMAGELENGTH, heights[i]);
      TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
      TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
      TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
      TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
      TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
      TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, heights[i]);

      if (i > 0) {
        TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
      }

      // Write data
      std::vector<uint8_t> data(widths[i] * heights[i]);
      for (uint32_t y = 0; y < heights[i]; ++y) {
        for (uint32_t x = 0; x < widths[i]; ++x) {
          data[y * widths[i] + x] =
              static_cast<uint8_t>((x + y + i * 50) % 256);
        }
      }
      TIFFWriteEncodedStrip(tif, 0, data.data(), data.size());

      // Write next directory (except for last one)
      if (i < widths.size() - 1) {
        TIFFWriteDirectory(tif);
      }
    }

    TIFFClose(tif);
  }

  /// @brief Create an invalid file for testing error handling
  void CreateInvalidTiffFile() {
    invalid_tiff_path_ = test_dir_ / "invalid.tiff";

    // Create a file with invalid TIFF header
    std::ofstream file(invalid_tiff_path_, std::ios::binary);
    file.write("Not a TIFF file", 15);
    file.close();
  }

  std::filesystem::path test_dir_;
  std::filesystem::path test_tiff_path_;
  std::filesystem::path test_strip_tiff_path_;
  std::filesystem::path test_multi_dir_tiff_path_;
  std::filesystem::path invalid_tiff_path_;
};

// Test factory method and validation
TEST_F(TiffFileTest, FactoryMethodAndValidation) {
  // Test successful creation
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  EXPECT_TRUE(tiff_file.IsValid());

  // Test null pool
  auto null_pool_result = TiffFile::Create(nullptr);
  EXPECT_FALSE(null_pool_result.ok());
  EXPECT_EQ(null_pool_result.status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(TiffFileTest, FileValidation) {
  // Test valid file
  auto valid_result = TiffFile::ValidateFile(test_tiff_path_);
  EXPECT_TRUE(valid_result.ok());

  // Test invalid file
  auto invalid_result = TiffFile::ValidateFile(invalid_tiff_path_);
  EXPECT_FALSE(invalid_result.ok());
  EXPECT_EQ(invalid_result.code(), absl::StatusCode::kInvalidArgument);

  // Test non-existent file
  auto nonexistent_result = TiffFile::ValidateFile("/non/existent/file.tiff");
  EXPECT_FALSE(nonexistent_result.ok());
  EXPECT_EQ(nonexistent_result.code(), absl::StatusCode::kInvalidArgument);
}

// Test directory operations
TEST_F(TiffFileTest, DirectoryOperations) {
  auto pool_result = TIFFHandlePool::Create(test_multi_dir_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test directory count
  auto dir_count_result = tiff_file.GetDirectoryCount();
  ASSERT_TRUE(dir_count_result.ok());
  EXPECT_EQ(dir_count_result.value(), 3);

  // Test directory count caching (should be faster second time)
  auto start_time = std::chrono::high_resolution_clock::now();
  auto cached_count_result = tiff_file.GetDirectoryCount();
  auto end_time = std::chrono::high_resolution_clock::now();
  auto cached_duration = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time);

  ASSERT_TRUE(cached_count_result.ok());
  EXPECT_EQ(cached_count_result.value(), 3);
  EXPECT_LT(cached_duration.count(), 10);  // Should be very fast (cached)

  // Test current directory
  EXPECT_EQ(tiff_file.GetCurrentDirectory(), 0);

  // Test setting directory
  auto set_dir_result = tiff_file.SetDirectory(1);
  EXPECT_TRUE(set_dir_result.ok());
  EXPECT_EQ(tiff_file.GetCurrentDirectory(), 1);

  // Test setting same directory (should be optimized)
  auto set_same_dir_result = tiff_file.SetDirectory(1);
  EXPECT_TRUE(set_same_dir_result.ok());
  EXPECT_EQ(tiff_file.GetCurrentDirectory(), 1);

  // Test setting invalid directory
  auto set_invalid_dir_result = tiff_file.SetDirectory(99);
  EXPECT_FALSE(set_invalid_dir_result.ok());
  EXPECT_EQ(set_invalid_dir_result.code(), absl::StatusCode::kInvalidArgument);
}

// Test image metadata extraction
TEST_F(TiffFileTest, ImageMetadata) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test image dimensions
  auto dims_result = tiff_file.GetImageDimensions();
  ASSERT_TRUE(dims_result.ok());
  EXPECT_EQ(dims_result.value()[0], kTestImageWidth);
  EXPECT_EQ(dims_result.value()[1], kTestImageHeight);

  // Test tile dimensions
  EXPECT_TRUE(tiff_file.IsTiled());
  auto tile_dims_result = tiff_file.GetTileDimensions();
  ASSERT_TRUE(tile_dims_result.ok());
  EXPECT_EQ(tile_dims_result.value()[0], kTestTileWidth);
  EXPECT_EQ(tile_dims_result.value()[1], kTestTileHeight);

  // Test photometric interpretation
  auto photometric_result = tiff_file.GetPhotometric();
  ASSERT_TRUE(photometric_result.ok());
  EXPECT_EQ(photometric_result.value(), TiffPhotometric::RGB);

  // Test compression
  auto compression_result = tiff_file.GetCompression();
  ASSERT_TRUE(compression_result.ok());
  EXPECT_EQ(compression_result.value(), TiffCompression::LZW);

  // Test samples per pixel
  auto spp_result = tiff_file.GetSamplesPerPixel();
  ASSERT_TRUE(spp_result.ok());
  EXPECT_EQ(spp_result.value(), 3);

  // Test bits per sample
  auto bps_result = tiff_file.GetBitsPerSample();
  ASSERT_TRUE(bps_result.ok());
  EXPECT_EQ(bps_result.value(), 8);

  // Test sample format
  auto sample_format_result = tiff_file.GetSampleFormat();
  ASSERT_TRUE(sample_format_result.ok());
  EXPECT_EQ(sample_format_result.value(), TiffSampleFormat::UInt);

  // Test planar configuration
  auto planar_config_result = tiff_file.GetPlanarConfig();
  ASSERT_TRUE(planar_config_result.ok());
  EXPECT_EQ(planar_config_result.value(), TiffPlanarConfig::Contig);

  // Test string fields
  EXPECT_EQ(tiff_file.GetImageDescription(), "Test TIFF file");
  EXPECT_EQ(tiff_file.GetSoftware(), "TiffFileTest");

  // Test resolution (use tolerance for floating point comparison)
  auto x_res = tiff_file.GetXResolution();
  ASSERT_TRUE(x_res.has_value());
  EXPECT_NEAR(x_res.value(), 72.0F, 1.0F);

  auto y_res = tiff_file.GetYResolution();
  ASSERT_TRUE(y_res.has_value());
  EXPECT_NEAR(y_res.value(), 72.0F, 1.0F);

  auto res_unit_result = tiff_file.GetResolutionUnit();
  EXPECT_EQ(res_unit_result, RESUNIT_INCH);

  // Test subfile type
  auto subfile_type_result = tiff_file.GetSubfileType();
  ASSERT_TRUE(subfile_type_result.ok());
  EXPECT_EQ(subfile_type_result.value(), 0);
}

TEST_F(TiffFileTest, GetDataType) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test data type conversion for 8-bit unsigned integer
  auto data_type_result = tiff_file.GetDataType();
  ASSERT_TRUE(data_type_result.ok());
  EXPECT_EQ(data_type_result.value(), DataType::kUInt8);
}

// Test comprehensive directory info
TEST_F(TiffFileTest, DirectoryInfo) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test comprehensive directory info
  auto dir_info_result = tiff_file.GetDirectoryInfo();
  ASSERT_TRUE(dir_info_result.ok());
  auto dir_info = dir_info_result.value();

  EXPECT_EQ(dir_info.image_dims[0], kTestImageWidth);
  EXPECT_EQ(dir_info.image_dims[1], kTestImageHeight);
  EXPECT_TRUE(dir_info.is_tiled);
  EXPECT_TRUE(dir_info.tile_dims.has_value());
  EXPECT_EQ(dir_info.tile_dims.value()[0], kTestTileWidth);
  EXPECT_EQ(dir_info.tile_dims.value()[1], kTestTileHeight);
  EXPECT_EQ(dir_info.samples_per_pixel, 3);
  EXPECT_EQ(dir_info.bits_per_sample, 8);
  EXPECT_EQ(dir_info.photometric, TiffPhotometric::RGB);
  EXPECT_EQ(dir_info.compression, TiffCompression::LZW);
  EXPECT_EQ(dir_info.sample_format, TiffSampleFormat::UInt);
  EXPECT_EQ(dir_info.planar_config, TiffPlanarConfig::Contig);
  EXPECT_EQ(dir_info.subfile_type, 0);
  EXPECT_TRUE(dir_info.image_description.has_value());
  EXPECT_EQ(dir_info.image_description.value(), "Test TIFF file");
  EXPECT_TRUE(dir_info.software.has_value());
  EXPECT_EQ(dir_info.software.value(), "TiffFileTest");
  EXPECT_TRUE(dir_info.x_resolution.has_value());
  EXPECT_NEAR(dir_info.x_resolution.value(), 72.0F, 1.0F);
  EXPECT_TRUE(dir_info.y_resolution.has_value());
  EXPECT_NEAR(dir_info.y_resolution.value(), 72.0F, 1.0F);
  EXPECT_EQ(dir_info.resolution_unit, RESUNIT_INCH);

  // Test utility methods
  EXPECT_EQ(dir_info.GetBytesPerPixel(), 3);
  EXPECT_FALSE(dir_info.IsReducedResolution());
  EXPECT_FALSE(dir_info.IsMask());
}

// Test reduced resolution detection
TEST_F(TiffFileTest, ReducedResolutionDetection) {
  auto pool_result = TIFFHandlePool::Create(test_multi_dir_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // First directory should not be reduced resolution
  auto dir_info_result = tiff_file.GetDirectoryInfo();
  ASSERT_TRUE(dir_info_result.ok());
  EXPECT_FALSE(dir_info_result.value().IsReducedResolution());

  // Second directory should be reduced resolution
  ASSERT_TRUE(tiff_file.SetDirectory(1).ok());
  auto dir_info_result2 = tiff_file.GetDirectoryInfo();
  ASSERT_TRUE(dir_info_result2.ok());
  EXPECT_TRUE(dir_info_result2.value().IsReducedResolution());
}

// Test reading operations
TEST_F(TiffFileTest, ReadingOperations) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test tile size
  auto tile_size_result = tiff_file.GetTileSize();
  ASSERT_TRUE(tile_size_result.ok());
  EXPECT_EQ(tile_size_result.value(), kTestTileWidth * kTestTileHeight * 3);

  // Test reading a tile
  std::vector<uint8_t> tile_buffer(tile_size_result.value());
  auto read_tile_result = tiff_file.ReadTile(tile_buffer.data(), {0, 0}, 0);
  EXPECT_TRUE(read_tile_result.ok());

  // Verify tile data pattern
  EXPECT_EQ(tile_buffer[0], 0);    // First pixel R should be 0
  EXPECT_EQ(tile_buffer[1], 0);    // First pixel G should be 0
  EXPECT_EQ(tile_buffer[2], 128);  // First pixel B should be 128
}

// Test scanline operations
TEST_F(TiffFileTest, ScanlineOperations) {
  auto pool_result = TIFFHandlePool::Create(test_strip_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Verify it's not tiled
  EXPECT_FALSE(tiff_file.IsTiled());

  // Test scanline size
  auto scanline_size_result = tiff_file.GetScanlineSize();
  ASSERT_TRUE(scanline_size_result.ok());
  EXPECT_EQ(scanline_size_result.value(), kTestImageWidth);

  // Test reading a scanline
  std::vector<uint8_t> scanline_buffer(scanline_size_result.value());
  auto read_scanline_result =
      tiff_file.ReadScanline(scanline_buffer.data(), 0, 0);
  EXPECT_TRUE(read_scanline_result.ok());

  // Verify scanline data pattern
  EXPECT_EQ(scanline_buffer[0], 0);  // First pixel should be 0
  EXPECT_EQ(scanline_buffer[1], 1);  // Second pixel should be 1
}

// Test error handling
TEST_F(TiffFileTest, ErrorHandling) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test reading scanline from tiled image (should fail)
  std::vector<uint8_t> buffer(1024);
  auto read_scanline_result = tiff_file.ReadScanline(buffer.data(), 0, 0);
  EXPECT_FALSE(read_scanline_result.ok());
  EXPECT_EQ(read_scanline_result.code(), absl::StatusCode::kFailedPrecondition);

  // Test reading invalid tile coordinates
  auto read_invalid_tile_result =
      tiff_file.ReadTile(buffer.data(), {999, 999}, 0);
  EXPECT_FALSE(read_invalid_tile_result.ok());
  EXPECT_EQ(read_invalid_tile_result.code(), absl::StatusCode::kInternal);
}

// Test strip-based file error handling
TEST_F(TiffFileTest, StripErrorHandling) {
  auto pool_result = TIFFHandlePool::Create(test_strip_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test reading tile from strip-based image (should fail)
  std::vector<uint8_t> buffer(1024);
  auto read_tile_result = tiff_file.ReadTile(buffer.data(), {0, 0}, 0);
  EXPECT_FALSE(read_tile_result.ok());
  EXPECT_EQ(read_tile_result.code(), absl::StatusCode::kFailedPrecondition);

  // Test getting tile size from strip-based image (should fail)
  auto tile_size_result = tiff_file.GetTileSize();
  EXPECT_FALSE(tile_size_result.ok());
  EXPECT_EQ(tile_size_result.status().code(),
            absl::StatusCode::kFailedPrecondition);

  // Test getting tile dimensions from strip-based image (should fail)
  auto tile_dims_result = tiff_file.GetTileDimensions();
  EXPECT_FALSE(tile_dims_result.ok());
  EXPECT_EQ(tile_dims_result.status().code(),
            absl::StatusCode::kFailedPrecondition);
}

// Test performance optimizations
TEST_F(TiffFileTest, PerformanceOptimizations) {
  auto pool_result = TIFFHandlePool::Create(test_multi_dir_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test directory count caching
  auto start_time = std::chrono::high_resolution_clock::now();
  auto first_count_result = tiff_file.GetDirectoryCount();
  auto middle_time = std::chrono::high_resolution_clock::now();
  auto second_count_result = tiff_file.GetDirectoryCount();
  auto end_time = std::chrono::high_resolution_clock::now();

  auto first_duration = std::chrono::duration_cast<std::chrono::microseconds>(
      middle_time - start_time);
  auto second_duration = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - middle_time);

  ASSERT_TRUE(first_count_result.ok());
  ASSERT_TRUE(second_count_result.ok());
  EXPECT_EQ(first_count_result.value(), second_count_result.value());

  // Second call should be faster or equal (cached)
  EXPECT_LE(second_duration.count(), first_duration.count());

  // Test SetDirectory optimization (setting to same directory)
  auto start_time2 = std::chrono::high_resolution_clock::now();
  auto set_dir_result1 = tiff_file.SetDirectory(0);
  auto middle_time2 = std::chrono::high_resolution_clock::now();
  auto set_dir_result2 = tiff_file.SetDirectory(0);  // Same directory
  auto end_time2 = std::chrono::high_resolution_clock::now();

  auto set_first_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(middle_time2 -
                                                            start_time2);
  auto set_second_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end_time2 -
                                                            middle_time2);

  EXPECT_TRUE(set_dir_result1.ok());
  EXPECT_TRUE(set_dir_result2.ok());

  // Second call should be faster or equal (early return)
  EXPECT_LE(set_second_duration.count(), set_first_duration.count());
}

// Test RGBA reading
TEST_F(TiffFileTest, RGBAReading) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  // Test RGBA reading
  std::vector<uint32_t> rgba_buffer(kTestImageWidth * kTestImageHeight);
  auto read_rgba_result = tiff_file.ReadRGBAImage(
      rgba_buffer.data(), kTestImageWidth, kTestImageHeight);
  EXPECT_TRUE(read_rgba_result.ok());

  // Verify we got some data
  EXPECT_NE(rgba_buffer[0], 0);
}

// Test move semantics
TEST_F(TiffFileTest, MoveSemantics) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file1 = std::move(tiff_file_result.value());

  EXPECT_TRUE(tiff_file1.IsValid());

  // Test move constructor
  auto tiff_file2 = std::move(tiff_file1);
  EXPECT_TRUE(tiff_file2.IsValid());

  // Test move assignment
  auto tiff_file3_result = TiffFile::Create(pool.get());
  ASSERT_TRUE(tiff_file3_result.ok());
  auto tiff_file3 = std::move(tiff_file3_result.value());

  tiff_file3 = std::move(tiff_file2);
  EXPECT_TRUE(tiff_file3.IsValid());
}

// Test convenience function
TEST_F(TiffFileTest, ConvenienceFunction) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto tiff_file_result = CreateTiffFile(pool.get());
  ASSERT_TRUE(tiff_file_result.ok());
  auto tiff_file = std::move(tiff_file_result.value());

  EXPECT_TRUE(tiff_file.IsValid());
}

}  // namespace fastslide
