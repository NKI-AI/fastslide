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

#include "simpletiff/predictor.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace simpletiff {
namespace {

// -----------------------------------------------------------
// Helper functions for encoding (differencing)
// These mirror libtiff's horDiff* functions
// -----------------------------------------------------------

// Byte swap helpers
static inline uint16_t Bswap16(uint16_t v) {
  return static_cast<uint16_t>((v << 8) | (v >> 8));
}

static inline uint32_t Bswap32(uint32_t v) {
  return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) |
         (v << 24);
}

template <class T>
inline T Byteswap(T v);

template <>
inline uint8_t Byteswap<uint8_t>(uint8_t v) {
  return v;
}

template <>
inline uint16_t Byteswap<uint16_t>(uint16_t v) {
  return Bswap16(v);
}

template <>
inline uint32_t Byteswap<uint32_t>(uint32_t v) {
  return Bswap32(v);
}

/// Apply horizontal differencing encoding (the inverse of the predictor)
/// This matches libtiff's horDiff* functions
template <class T>
inline void ApplyHorizontalDifferencing(T* row, uint32_t width,
                                        uint32_t samples_per_pixel) {
  if (width == 0)
    return;
  const uint32_t stride = samples_per_pixel;
  const uint32_t N = width * stride;

  // Work backwards like libtiff does
  for (uint32_t i = N - 1; i >= stride; --i) {
    row[i] = static_cast<T>(row[i] - row[i - stride]);
  }
}

template <class T>
inline void ApplyHorizontalDifferencingBytes(void* row_bytes, uint32_t width,
                                             uint32_t samples_per_pixel,
                                             bool file_big_endian) {
  T* row = reinterpret_cast<T*>(row_bytes);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (file_big_endian) {
    const uint32_t N = width * samples_per_pixel;
    for (uint32_t i = 0; i < N; ++i)
      row[i] = Byteswap<T>(row[i]);
  }
#endif

  ApplyHorizontalDifferencing<T>(row, width, samples_per_pixel);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (file_big_endian) {
    const uint32_t N = width * samples_per_pixel;
    for (uint32_t i = 0; i < N; ++i)
      row[i] = Byteswap<T>(row[i]);
  }
#else
  (void)file_big_endian;
#endif
}

/// Encode data with horizontal predictor (Predictor = 2)
void EncodeHorizontalPredictor(std::vector<uint8_t>& data, int width,
                               int height, int samples_per_pixel,
                               int bits_per_sample, bool file_big_endian,
                               int planar_configuration = 1) {
  const uint32_t bytes_per_sample = static_cast<uint32_t>(bits_per_sample / 8);
  uint8_t* p = data.data();

  if (planar_configuration == 1) {
    // Interleaved layout: RGBRGBRGB...
    const uint32_t row_stride_bytes =
        static_cast<uint32_t>(width * samples_per_pixel * bytes_per_sample);
    for (int y = 0; y < height; ++y) {
      uint8_t* row_start = p + static_cast<size_t>(y) * row_stride_bytes;
      switch (bits_per_sample) {
        case 8:
          ApplyHorizontalDifferencingBytes<uint8_t>(
              row_start, static_cast<uint32_t>(width),
              static_cast<uint32_t>(samples_per_pixel), file_big_endian);
          break;
        case 16:
          ApplyHorizontalDifferencingBytes<uint16_t>(
              row_start, static_cast<uint32_t>(width),
              static_cast<uint32_t>(samples_per_pixel), file_big_endian);
          break;
        case 32:
          ApplyHorizontalDifferencingBytes<uint32_t>(
              row_start, static_cast<uint32_t>(width),
              static_cast<uint32_t>(samples_per_pixel), file_big_endian);
          break;
        default:
          ADD_FAILURE() << "Unsupported BitsPerSample for encoding: "
                        << bits_per_sample;
          return;
      }
    }
  } else if (planar_configuration == 2) {
    // Separate planes: RRR...GGG...BBB...
    const uint32_t plane_row_stride_bytes =
        static_cast<uint32_t>(width * bytes_per_sample);
    const uint32_t plane_size_bytes =
        plane_row_stride_bytes * static_cast<uint32_t>(height);

    for (int c = 0; c < samples_per_pixel; ++c) {
      uint8_t* plane_start = p + c * plane_size_bytes;
      for (int y = 0; y < height; ++y) {
        uint8_t* row_start =
            plane_start + static_cast<size_t>(y) * plane_row_stride_bytes;
        switch (bits_per_sample) {
          case 8:
            // stride = 1 for planar (single channel per plane)
            ApplyHorizontalDifferencingBytes<uint8_t>(
                row_start, static_cast<uint32_t>(width), 1, file_big_endian);
            break;
          case 16:
            ApplyHorizontalDifferencingBytes<uint16_t>(
                row_start, static_cast<uint32_t>(width), 1, file_big_endian);
            break;
          case 32:
            ApplyHorizontalDifferencingBytes<uint32_t>(
                row_start, static_cast<uint32_t>(width), 1, file_big_endian);
            break;
          default:
            ADD_FAILURE() << "Unsupported BitsPerSample for encoding: "
                          << bits_per_sample;
            return;
        }
      }
    }
  }
}

// Helper function to read binary test data file
std::vector<uint8_t> ReadTestDataFile(const char* filename) {
  // Try multiple possible paths (depending on where test is run from)
  const char* paths[] = {nullptr,  // Will be set to filename with path prefix
                         nullptr, nullptr};

  std::string path1 = std::string("testdata/") + filename;
  std::string path2 = std::string("../testdata/") + filename;
  std::string path3 = std::string("../../testdata/") + filename;

  paths[0] = path1.c_str();
  paths[1] = path2.c_str();
  paths[2] = path3.c_str();

  FILE* f = nullptr;
  for (const char* path : paths) {
    f = fopen(path, "rb");
    if (f)
      break;
  }

  if (!f) {
    return {};
  }

  fseek(f, 0, SEEK_END);
  int64_t size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(size);
  fread(data.data(), 1, size, f);
  fclose(f);
  return data;
}

// -----------------------------------------------------------
// Tests for 8-bit data (CONTIG)
// -----------------------------------------------------------

TEST(PredictorTest, Acc8Grayscale) {
  // Test 8-bit grayscale (1 sample per pixel, CONTIG)
  const int width = 10;
  const int height = 1;
  const int samples_per_pixel = 1;
  const int bits_per_sample = 8;
  const bool big_endian = false;

  // Original data: simple sequence
  std::vector<uint8_t> original = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  // After differencing: first pixel unchanged, rest are differences
  // Expected: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
  std::vector<uint8_t> expected_diff = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

  // Encode (apply differencing)
  std::vector<uint8_t> encoded = original;
  EncodeHorizontalPredictor(encoded, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  EXPECT_EQ(encoded, expected_diff);

  // Decode (apply accumulation) - should get back original
  ASSERT_TRUE(ApplyHorizontalPredictor(encoded, width, height,
                                       samples_per_pixel, bits_per_sample,
                                       big_endian, 1)
                  .ok());
  EXPECT_EQ(encoded, original);
}

TEST(PredictorTest, Acc8RGB) {
  // Test 8-bit RGB (3 samples per pixel, CONTIG)
  const int width = 4;
  const int height = 1;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 8;
  const bool big_endian = false;

  // Original: R G B | R G B | R G B | R G B
  std::vector<uint8_t> original = {10, 20, 30,   // pixel 0
                                   15, 25, 35,   // pixel 1: +5, +5, +5
                                   20, 30, 40,   // pixel 2: +5, +5, +5
                                   25, 35, 45};  // pixel 3: +5, +5, +5

  // After differencing (stride=3):
  // pixel 0: unchanged [10, 20, 30]
  // pixel 1: [15-10, 25-20, 35-30] = [5, 5, 5]
  // pixel 2: [20-15, 30-25, 40-35] = [5, 5, 5]
  // pixel 3: [25-20, 35-30, 45-40] = [5, 5, 5]
  std::vector<uint8_t> expected_diff = {10, 20, 30, 5, 5, 5, 5, 5, 5, 5, 5, 5};

  // Encode
  std::vector<uint8_t> encoded = original;
  EncodeHorizontalPredictor(encoded, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  EXPECT_EQ(encoded, expected_diff);

  // Decode - should get back original
  ASSERT_TRUE(ApplyHorizontalPredictor(encoded, width, height,
                                       samples_per_pixel, bits_per_sample,
                                       big_endian, 1)
                  .ok());
  EXPECT_EQ(encoded, original);
}

TEST(PredictorTest, Acc8RGBA) {
  // Test 8-bit RGBA (4 samples per pixel, CONTIG)
  const int width = 3;
  const int height = 1;
  const int samples_per_pixel = 4;
  const int bits_per_sample = 8;
  const bool big_endian = false;

  // Original: R G B A | R G B A | R G B A
  std::vector<uint8_t> original = {
      100, 110, 120, 255,   // pixel 0
      110, 120, 130, 255,   // pixel 1: +10 each RGB
      120, 130, 140, 255};  // pixel 2: +10 each RGB

  // Round-trip test
  std::vector<uint8_t> encoded = original;
  EncodeHorizontalPredictor(encoded, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(encoded, width, height,
                                       samples_per_pixel, bits_per_sample,
                                       big_endian, 1)
                  .ok());
  EXPECT_EQ(encoded, original);
}

// -----------------------------------------------------------
// Tests for 16-bit data (CONTIG)
// -----------------------------------------------------------

TEST(PredictorTest, Acc16Grayscale) {
  const int width = 5;
  const int height = 1;
  const int samples_per_pixel = 1;
  const int bits_per_sample = 16;
  const bool big_endian = false;

  // Create original 16-bit data
  std::vector<uint16_t> original_u16 = {100, 200, 300, 400, 500};
  std::vector<uint8_t> original(original_u16.size() * 2);
  std::memcpy(original.data(), original_u16.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, Acc16RGB) {
  const int width = 3;
  const int height = 1;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 16;
  const bool big_endian = false;

  // R G B | R G B | R G B
  std::vector<uint16_t> original_u16 = {1000, 2000, 3000,   // pixel 0
                                        1100, 2100, 3100,   // pixel 1
                                        1200, 2200, 3200};  // pixel 2
  std::vector<uint8_t> original(original_u16.size() * 2);
  std::memcpy(original.data(), original_u16.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

// -----------------------------------------------------------
// Tests for 32-bit data (CONTIG)
// -----------------------------------------------------------

TEST(PredictorTest, Acc32Grayscale) {
  const int width = 4;
  const int height = 1;
  const int samples_per_pixel = 1;
  const int bits_per_sample = 32;
  const bool big_endian = false;

  std::vector<uint32_t> original_u32 = {10000, 20000, 30000, 40000};
  std::vector<uint8_t> original(original_u32.size() * 4);
  std::memcpy(original.data(), original_u32.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, Acc32RGB) {
  const int width = 2;
  const int height = 1;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 32;
  const bool big_endian = false;

  std::vector<uint32_t> original_u32 = {100000, 200000, 300000,   // pixel 0
                                        110000, 210000, 310000};  // pixel 1
  std::vector<uint8_t> original(original_u32.size() * 4);
  std::memcpy(original.data(), original_u32.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

// -----------------------------------------------------------
// Tests for PLANAR configuration (SEPARATE)
// -----------------------------------------------------------

TEST(PredictorTest, Acc8RGBPlanar) {
  // Test 8-bit RGB with planar configuration (SEPARATE)
  const int width = 4;
  const int height = 1;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 8;
  const bool big_endian = false;
  const int planar_config = 2;  // SEPARATE

  // Planar layout: RRRR GGGG BBBB
  std::vector<uint8_t> original = {10, 15, 20, 25,   // R plane
                                   20, 25, 30, 35,   // G plane
                                   30, 35, 40, 45};  // B plane

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, planar_config);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian,
                                       planar_config)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, Acc16RGBPlanar) {
  const int width = 3;
  const int height = 1;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 16;
  const bool big_endian = false;
  const int planar_config = 2;  // SEPARATE

  // Planar: RRR GGG BBB
  std::vector<uint16_t> original_u16 = {1000, 1100, 1200,   // R plane
                                        2000, 2100, 2200,   // G plane
                                        3000, 3100, 3200};  // B plane
  std::vector<uint8_t> original(original_u16.size() * 2);
  std::memcpy(original.data(), original_u16.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, planar_config);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian,
                                       planar_config)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, Acc32RGBPlanar) {
  const int width = 2;
  const int height = 1;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 32;
  const bool big_endian = false;
  const int planar_config = 2;  // SEPARATE

  std::vector<uint32_t> original_u32 = {100000, 110000,   // R plane
                                        200000, 210000,   // G plane
                                        300000, 310000};  // B plane
  std::vector<uint8_t> original(original_u32.size() * 4);
  std::memcpy(original.data(), original_u32.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, planar_config);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian,
                                       planar_config)
                  .ok());
  EXPECT_EQ(data, original);
}

// -----------------------------------------------------------
// Tests for multiple rows
// -----------------------------------------------------------

TEST(PredictorTest, MultiRow8RGB) {
  const int width = 3;
  const int height = 3;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 8;
  const bool big_endian = false;

  // 3x3 RGB image
  std::vector<uint8_t> original = {// Row 0
                                   10, 20, 30, 15, 25, 35, 20, 30, 40,
                                   // Row 1
                                   50, 60, 70, 55, 65, 75, 60, 70, 80,
                                   // Row 2
                                   90, 100, 110, 95, 105, 115, 100, 110, 120};

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, MultiRow16RGB) {
  const int width = 2;
  const int height = 2;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 16;
  const bool big_endian = false;

  std::vector<uint16_t> original_u16 = {// Row 0
                                        1000, 2000, 3000, 1100, 2100, 3100,
                                        // Row 1
                                        4000, 5000, 6000, 4100, 5100, 6100};
  std::vector<uint8_t> original(original_u16.size() * 2);
  std::memcpy(original.data(), original_u16.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

// -----------------------------------------------------------
// Tests for big endian
// -----------------------------------------------------------

TEST(PredictorTest, Acc16BigEndian) {
  const int width = 4;
  const int height = 1;
  const int samples_per_pixel = 1;
  const int bits_per_sample = 16;
  const bool big_endian = true;

  std::vector<uint16_t> original_u16 = {1000, 2000, 3000, 4000};
  std::vector<uint8_t> original(original_u16.size() * 2);

  // Convert to big endian
  for (size_t i = 0; i < original_u16.size(); ++i) {
    uint16_t val = Bswap16(original_u16[i]);
    std::memcpy(&original[i * 2], &val, 2);
  }

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, Acc32BigEndian) {
  const int width = 3;
  const int height = 1;
  const int samples_per_pixel = 1;
  const int bits_per_sample = 32;
  const bool big_endian = true;

  std::vector<uint32_t> original_u32 = {10000, 20000, 30000};
  std::vector<uint8_t> original(original_u32.size() * 4);

  // Convert to big endian
  for (size_t i = 0; i < original_u32.size(); ++i) {
    uint32_t val = Bswap32(original_u32[i]);
    std::memcpy(&original[i * 4], &val, 4);
  }

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

// -----------------------------------------------------------
// Tests with random-like data
// -----------------------------------------------------------

TEST(PredictorTest, RoundTrip8RandomData) {
  const int width = 100;
  const int height = 10;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 8;
  const bool big_endian = false;

  // Create pseudo-random data
  std::vector<uint8_t> original(width * height * samples_per_pixel);
  for (size_t i = 0; i < original.size(); ++i) {
    original[i] = static_cast<uint8_t>((i * 7919 + 997) % 256);
  }

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, RoundTrip16RandomData) {
  const int width = 50;
  const int height = 5;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 16;
  const bool big_endian = false;

  std::vector<uint16_t> original_u16(width * height * samples_per_pixel);
  for (size_t i = 0; i < original_u16.size(); ++i) {
    original_u16[i] = static_cast<uint16_t>((i * 31337 + 42) % 65536);
  }
  std::vector<uint8_t> original(original_u16.size() * 2);
  std::memcpy(original.data(), original_u16.data(), original.size());

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, 1);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian, 1)
                  .ok());
  EXPECT_EQ(data, original);
}

TEST(PredictorTest, RoundTrip8PlanarRandomData) {
  const int width = 80;
  const int height = 8;
  const int samples_per_pixel = 3;
  const int bits_per_sample = 8;
  const bool big_endian = false;
  const int planar_config = 2;  // SEPARATE

  std::vector<uint8_t> original(width * height * samples_per_pixel);
  for (size_t i = 0; i < original.size(); ++i) {
    original[i] = static_cast<uint8_t>((i * 12345 + 678) % 256);
  }

  // Round-trip test
  std::vector<uint8_t> data = original;
  EncodeHorizontalPredictor(data, width, height, samples_per_pixel,
                            bits_per_sample, big_endian, planar_config);
  ASSERT_TRUE(ApplyHorizontalPredictor(data, width, height, samples_per_pixel,
                                       bits_per_sample, big_endian,
                                       planar_config)
                  .ok());
  EXPECT_EQ(data, original);
}

}  // namespace
}  // namespace simpletiff
