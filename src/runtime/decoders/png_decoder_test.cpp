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

#include "fastslide/runtime/decoders/png_decoder.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lodepng/lodepng.h"

namespace fastslide::runtime::decoders {
namespace {

constexpr uint32_t kWidth = 4;
constexpr uint32_t kHeight = 3;

/// @brief Build a deterministic 4x3 RGBA8 gradient.
std::vector<uint8_t> MakeRgbaGradient() {
  std::vector<uint8_t> pixels(kWidth * kHeight * 4);
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      const std::size_t idx = (y * kWidth + x) * 4U;
      pixels[idx + 0] = static_cast<uint8_t>(x * 50);
      pixels[idx + 1] = static_cast<uint8_t>(y * 70);
      pixels[idx + 2] = static_cast<uint8_t>((x + y) * 30);
      pixels[idx + 3] = 0xFF;
    }
  }
  return pixels;
}

/// @brief Build a unique temporary file path under the system temp dir.
std::string MakeTempPath(const std::string& suffix) {
  const char* dir = std::getenv("TEST_TMPDIR");
  if (dir == nullptr) {
    dir = "/tmp";
  }
  static int counter = 0;
  std::string path(dir);
  path += "/fastslide_png_decoder_test_";
  path += std::to_string(::testing::UnitTest::GetInstance()->random_seed());
  path += "_";
  path += std::to_string(counter++);
  path += suffix;
  return path;
}

TEST(PngDecoderTest, RoundTripRgbViaFile) {
  const auto rgba = MakeRgbaGradient();
  const std::string path = MakeTempPath(".png");

  ASSERT_TRUE(
      EncodePngToFile(path, rgba, kWidth, kHeight, /*channels=*/4).ok());

  const auto decoded_or = DecodePngFileToRgb(path);
  ASSERT_TRUE(decoded_or.ok()) << decoded_or.status().message();
  const auto& decoded = decoded_or.value();

  EXPECT_EQ(decoded.width, kWidth);
  EXPECT_EQ(decoded.height, kHeight);
  ASSERT_EQ(decoded.rgb.size(), kWidth * kHeight * 3U);

  for (uint32_t i = 0; i < kWidth * kHeight; ++i) {
    EXPECT_EQ(decoded.rgb[i * 3 + 0], rgba[i * 4 + 0]) << "at pixel " << i;
    EXPECT_EQ(decoded.rgb[i * 3 + 1], rgba[i * 4 + 1]) << "at pixel " << i;
    EXPECT_EQ(decoded.rgb[i * 3 + 2], rgba[i * 4 + 2]) << "at pixel " << i;
  }

  std::remove(path.c_str());
}

TEST(PngDecoderTest, RoundTripRgbaViaFile) {
  const auto rgba = MakeRgbaGradient();
  const std::string path = MakeTempPath(".png");

  ASSERT_TRUE(
      EncodePngToFile(path, rgba, kWidth, kHeight, /*channels=*/4).ok());

  const auto decoded_or = DecodePngFileToRgba(path);
  ASSERT_TRUE(decoded_or.ok()) << decoded_or.status().message();
  const auto& decoded = decoded_or.value();

  EXPECT_EQ(decoded.width, kWidth);
  EXPECT_EQ(decoded.height, kHeight);
  ASSERT_EQ(decoded.rgba.size(), rgba.size());
  EXPECT_EQ(decoded.rgba, rgba);

  std::remove(path.c_str());
}

TEST(PngDecoderTest, EmptyInputIsInvalidArgument) {
  const std::vector<uint8_t> empty;
  const auto rgb_or = DecodePngToRgb(empty);
  ASSERT_FALSE(rgb_or.ok());
  EXPECT_EQ(rgb_or.status().code(), aifocore::StatusCode::kInvalidArgument);

  const auto rgba_or = DecodePngToRgba(empty);
  ASSERT_FALSE(rgba_or.ok());
  EXPECT_EQ(rgba_or.status().code(), aifocore::StatusCode::kInvalidArgument);
}

TEST(PngDecoderTest, BogusPngBytesIsInternal) {
  const std::vector<uint8_t> garbage(64, 0xAB);
  const auto rgb_or = DecodePngToRgb(garbage);
  ASSERT_FALSE(rgb_or.ok());
  EXPECT_EQ(rgb_or.status().code(), aifocore::StatusCode::kInternal);
}

TEST(PngDecoderTest, EncodeRejectsBadChannelCount) {
  const std::vector<uint8_t> pixels(4U * 4U * 2U, 0);
  const std::string path = MakeTempPath(".png");
  const auto status = EncodePngToFile(path, pixels, 4, 4, /*channels=*/2);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), aifocore::StatusCode::kInvalidArgument);
}

TEST(PngDecoderTest, EncodeRejectsBufferSizeMismatch) {
  const std::vector<uint8_t> too_small(10, 0);
  const std::string path = MakeTempPath(".png");
  const auto status = EncodePngToFile(path, too_small, 4, 4, /*channels=*/3);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), aifocore::StatusCode::kInvalidArgument);
}

namespace {

/// @brief Encode a 16-bit RGB buffer (host endianness) into a PNG bitstream.
///
/// lodepng's PNG container stores 16-bit samples big-endian, so the host
/// values are byte-swapped before encoding.
std::vector<unsigned char> EncodeRgb16Png(const std::vector<uint16_t>& host_rgb,
                                          uint32_t width, uint32_t height) {
  std::vector<unsigned char> raw;
  raw.reserve(host_rgb.size() * 2U);
  for (uint16_t value : host_rgb) {
    raw.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    raw.push_back(static_cast<unsigned char>(value & 0xFF));
  }
  std::vector<unsigned char> png;
  const unsigned int err =
      lodepng::encode(png, raw, width, height, LCT_RGB, 16);
  EXPECT_EQ(err, 0U) << lodepng_error_text(err);
  return png;
}

}  // namespace

TEST(PngDecoderTest, Decode16BitRgbRoundTrip) {
  constexpr uint32_t kW = 5;
  constexpr uint32_t kH = 4;
  std::vector<uint16_t> rgb(kW * kH * 3U);
  for (uint32_t y = 0; y < kH; ++y) {
    for (uint32_t x = 0; x < kW; ++x) {
      const std::size_t base = (y * kW + x) * 3U;
      rgb[base + 0] = static_cast<uint16_t>(x * 4096U + 7U);
      rgb[base + 1] = static_cast<uint16_t>(y * 8192U + 13U);
      rgb[base + 2] = static_cast<uint16_t>((x + y) * 1024U + 257U);
    }
  }

  const auto png = EncodeRgb16Png(rgb, kW, kH);
  ASSERT_FALSE(png.empty());

  const auto decoded_or =
      DecodePng16ToRgb(std::span<const uint8_t>(png.data(), png.size()));
  ASSERT_TRUE(decoded_or.ok()) << decoded_or.status().message();
  const auto& decoded = decoded_or.value();

  EXPECT_EQ(decoded.width, kW);
  EXPECT_EQ(decoded.height, kH);
  ASSERT_EQ(decoded.rgb.size(), rgb.size());
  EXPECT_EQ(decoded.rgb, rgb);
}

TEST(PngDecoderTest, Decode16BitFromUpconverted8Bit) {
  // 8-bit PNGs decoded as 16-bit are scaled to occupy the full 16-bit range
  // (lodepng replicates the byte: v -> v*0x101).
  const auto rgba8 = MakeRgbaGradient();
  const std::string path = MakeTempPath(".png");
  ASSERT_TRUE(
      EncodePngToFile(path, rgba8, kWidth, kHeight, /*channels=*/4).ok());

  std::vector<unsigned char> file_bytes;
  ASSERT_EQ(lodepng::load_file(file_bytes, path), 0U);
  std::remove(path.c_str());

  const auto decoded_or = DecodePng16ToRgb(
      std::span<const uint8_t>(file_bytes.data(), file_bytes.size()));
  ASSERT_TRUE(decoded_or.ok()) << decoded_or.status().message();
  const auto& decoded = decoded_or.value();
  EXPECT_EQ(decoded.width, kWidth);
  EXPECT_EQ(decoded.height, kHeight);
  ASSERT_EQ(decoded.rgb.size(), kWidth * kHeight * 3U);
  for (uint32_t i = 0; i < kWidth * kHeight; ++i) {
    EXPECT_EQ(decoded.rgb[i * 3 + 0],
              static_cast<uint16_t>(rgba8[i * 4 + 0] * 0x101U));
    EXPECT_EQ(decoded.rgb[i * 3 + 1],
              static_cast<uint16_t>(rgba8[i * 4 + 1] * 0x101U));
    EXPECT_EQ(decoded.rgb[i * 3 + 2],
              static_cast<uint16_t>(rgba8[i * 4 + 2] * 0x101U));
  }
}

TEST(PngDecoderTest, Decode16BitRejectsEmpty) {
  const std::vector<uint8_t> empty;
  const auto rgb_or = DecodePng16ToRgb(empty);
  ASSERT_FALSE(rgb_or.ok());
  EXPECT_EQ(rgb_or.status().code(), aifocore::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace fastslide::runtime::decoders
