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

#include "fastslide/runtime/decoders/bmp_decoder.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"

namespace fastslide::runtime::decoders {
namespace {

/// @brief Build a minimal 24-bit uncompressed BMP file in memory.
///
/// `bgr_rows[y]` holds the BGR bytes for source row `y` (without padding).
/// When `top_down` is true, height is encoded as a negative value.
std::vector<uint8_t> BuildBmp24(int32_t width, int32_t height, bool top_down,
                                const std::vector<std::vector<uint8_t>>& rows) {
  const uint32_t row_stride = ((static_cast<uint32_t>(width) * 3U) + 3U) & ~3U;
  const uint32_t pixel_bytes = row_stride * static_cast<uint32_t>(height);
  const uint32_t data_offset = 54;
  const uint32_t file_size = data_offset + pixel_bytes;

  std::vector<uint8_t> bmp(file_size, 0);
  bmp[0] = 'B';
  bmp[1] = 'M';
  std::memcpy(&bmp[2], &file_size, 4);
  std::memcpy(&bmp[10], &data_offset, 4);

  const uint32_t info_size = 40;
  std::memcpy(&bmp[14], &info_size, 4);
  std::memcpy(&bmp[18], &width, 4);
  const int32_t height_field = top_down ? -height : height;
  std::memcpy(&bmp[22], &height_field, 4);
  const uint16_t planes = 1;
  std::memcpy(&bmp[26], &planes, 2);
  const uint16_t bpp = 24;
  std::memcpy(&bmp[28], &bpp, 2);

  for (int32_t y = 0; y < height; ++y) {
    std::memcpy(&bmp[data_offset + static_cast<uint32_t>(y) * row_stride],
                rows[static_cast<std::size_t>(y)].data(),
                rows[static_cast<std::size_t>(y)].size());
  }
  return bmp;
}

TEST(BmpDecoderTest, DecodesBottomUpBgrToRgb) {
  // 2x2 image: rows on disk are bottom-up.
  // On-disk row 0 = bottom of image; row 1 = top.
  // After decode, row 0 of the result must be the top of the image.
  // So we build:
  //   on-disk row 0 (bottom): pixels = top-left of image and top-right? No.
  //
  // Let's pick distinct colours:
  //   image(0,0) RGB = (10,20,30) -> top-left
  //   image(1,0) RGB = (40,50,60) -> top-right
  //   image(0,1) RGB = (70,80,90) -> bottom-left
  //   image(1,1) RGB = (100,110,120) -> bottom-right
  // Bottom-up storage: file row 0 = (bottom-left, bottom-right) as BGR.
  std::vector<uint8_t> file_row0 = {
      /*BL B*/ 90,  /*G*/ 80,  /*R*/ 70,
      /*BR B*/ 120, /*G*/ 110, /*R*/ 100};
  std::vector<uint8_t> file_row1 = {/*TL B*/ 30, /*G*/ 20, /*R*/ 10,
                                    /*TR B*/ 60, /*G*/ 50, /*R*/ 40};
  // Pad each row to 4-byte boundary.
  file_row0.resize(((2 * 3) + 3) & ~3, 0);
  file_row1.resize(((2 * 3) + 3) & ~3, 0);

  const auto bmp = BuildBmp24(2, 2, /*top_down=*/false, {file_row0, file_row1});

  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_TRUE(out_or.ok()) << out_or.status().message();
  const auto& out = out_or.value();
  EXPECT_EQ(out.width, 2U);
  EXPECT_EQ(out.height, 2U);
  ASSERT_EQ(out.rgb.size(), 2U * 2U * 3U);

  // Expected (top-down) RGB layout:
  //   (10,20,30) (40,50,60)
  //   (70,80,90) (100,110,120)
  const std::vector<uint8_t> expected = {10, 20, 30, 40,  50,  60,
                                         70, 80, 90, 100, 110, 120};
  EXPECT_EQ(out.rgb, expected);
}

TEST(BmpDecoderTest, DecodesTopDownBgrToRgb) {
  std::vector<uint8_t> file_row0 = {/*TL B*/ 30, /*G*/ 20, /*R*/ 10,
                                    /*TR B*/ 60, /*G*/ 50, /*R*/ 40};
  std::vector<uint8_t> file_row1 = {
      /*BL B*/ 90,  /*G*/ 80,  /*R*/ 70,
      /*BR B*/ 120, /*G*/ 110, /*R*/ 100};
  file_row0.resize(((2 * 3) + 3) & ~3, 0);
  file_row1.resize(((2 * 3) + 3) & ~3, 0);

  const auto bmp = BuildBmp24(2, 2, /*top_down=*/true, {file_row0, file_row1});

  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_TRUE(out_or.ok()) << out_or.status().message();
  const auto& out = out_or.value();
  const std::vector<uint8_t> expected = {10, 20, 30, 40,  50,  60,
                                         70, 80, 90, 100, 110, 120};
  EXPECT_EQ(out.rgb, expected);
}

TEST(BmpDecoderTest, RejectsTooSmallBuffer) {
  const std::vector<uint8_t> tiny(20, 0);
  const auto out_or = DecodeBmpToRgb(tiny);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kInvalidArgument);
}

TEST(BmpDecoderTest, RejectsBadSignature) {
  std::vector<uint8_t> bmp(54, 0);
  bmp[0] = 'X';
  bmp[1] = 'Y';
  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kInvalidArgument);
}

TEST(BmpDecoderTest, RejectsNon24BitDepth) {
  std::vector<uint8_t> bmp = BuildBmp24(
      2, 2, false, {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}});
  // Patch bpp to 32.
  const uint16_t bpp = 32;
  std::memcpy(&bmp[28], &bpp, 2);
  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kUnimplemented);
}

TEST(BmpDecoderTest, RejectsTruncatedPixelData) {
  const auto bmp_full = BuildBmp24(
      2, 2, false, {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}});
  // Drop the last few bytes to truncate the pixel array.
  std::vector<uint8_t> bmp(bmp_full.begin(), bmp_full.end() - 4);
  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kInvalidArgument);
}

TEST(BmpDecoderTest, RejectsCompressedBmp) {
  std::vector<uint8_t> bmp = BuildBmp24(
      2, 2, false, {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}});
  // Patch biCompression (offset 30) to BITFIELDS (3); BMP must be BI_RGB (0).
  const uint32_t compression = 3;
  std::memcpy(&bmp[30], &compression, 4);
  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kUnimplemented);
}

TEST(BmpDecoderTest, RejectsMultiplePlanes) {
  std::vector<uint8_t> bmp = BuildBmp24(
      2, 2, false, {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}});
  // Patch biPlanes (offset 26) to 2; only 1 is legal.
  const uint16_t planes = 2;
  std::memcpy(&bmp[26], &planes, 2);
  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kUnimplemented);
}

TEST(BmpDecoderTest, RejectsTooSmallInfoHeader) {
  std::vector<uint8_t> bmp = BuildBmp24(
      2, 2, false, {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}});
  // Patch biSize (offset 14) to 12 (OS/2 1.x BITMAPCOREHEADER, unsupported).
  const uint32_t info_size = 12;
  std::memcpy(&bmp[14], &info_size, 4);
  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kUnimplemented);
}

TEST(BmpDecoderTest, DecodesUnpaddedSingleColumnRow) {
  // width=1 already needs 3 bytes, padded to 4. Verifies stride math when the
  // unpadded row size and the aligned row stride differ.
  const std::vector<uint8_t> row0 = {/*B*/ 30, /*G*/ 20, /*R*/ 10, /*pad*/ 0};
  const std::vector<uint8_t> row1 = {/*B*/ 60, /*G*/ 50, /*R*/ 40, /*pad*/ 0};
  // Bottom-up: file row 0 is the bottom of the image.
  const auto bmp = BuildBmp24(1, 2, /*top_down=*/false, {row0, row1});

  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_TRUE(out_or.ok()) << out_or.status().message();
  const auto& out = out_or.value();
  EXPECT_EQ(out.width, 1U);
  EXPECT_EQ(out.height, 2U);
  // Top-down RGB: top row was file row 1 = (40,50,60); bottom = (10,20,30).
  const std::vector<uint8_t> expected = {40, 50, 60, 10, 20, 30};
  EXPECT_EQ(out.rgb, expected);
}

TEST(BmpDecoderTest, DecodesLargerImage) {
  // 16x4 image with deterministic gradient. Exercises the row loop more than
  // the trivial 2x2 cases above and acts as a regression for stride math.
  constexpr int32_t kW = 16;
  constexpr int32_t kH = 4;
  const uint32_t row_stride = ((static_cast<uint32_t>(kW) * 3U) + 3U) & ~3U;
  std::vector<std::vector<uint8_t>> file_rows(kH,
                                              std::vector<uint8_t>(row_stride));
  for (int32_t y = 0; y < kH; ++y) {
    for (int32_t x = 0; x < kW; ++x) {
      // file_rows[y] holds the bottom-up row, so image_y = (kH-1-y).
      const int32_t image_y = (kH - 1 - y);
      file_rows[y][x * 3 + 0] = static_cast<uint8_t>(image_y * 17);  // B
      file_rows[y][x * 3 + 1] = static_cast<uint8_t>(x * 5);         // G
      file_rows[y][x * 3 + 2] = static_cast<uint8_t>(image_y + x);   // R
    }
  }

  const auto bmp = BuildBmp24(kW, kH, /*top_down=*/false, file_rows);
  const auto out_or = DecodeBmpToRgb(bmp);
  ASSERT_TRUE(out_or.ok()) << out_or.status().message();
  const auto& out = out_or.value();
  ASSERT_EQ(out.rgb.size(),
            static_cast<std::size_t>(kW) * static_cast<std::size_t>(kH) * 3U);

  for (int32_t y = 0; y < kH; ++y) {
    for (int32_t x = 0; x < kW; ++x) {
      const std::size_t idx =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(kW) +
           static_cast<std::size_t>(x)) *
          3U;
      EXPECT_EQ(out.rgb[idx + 0], static_cast<uint8_t>(y + x));
      EXPECT_EQ(out.rgb[idx + 1], static_cast<uint8_t>(x * 5));
      EXPECT_EQ(out.rgb[idx + 2], static_cast<uint8_t>(y * 17));
    }
  }
}

}  // namespace
}  // namespace fastslide::runtime::decoders
