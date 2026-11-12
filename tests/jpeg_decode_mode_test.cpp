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

// Tests for `SlideReader::SetJpegDecodeMode` and the decode-mode plumbing:
// FastSlide's own JPEG decoder on a synthetic JPEG, and the simpletiff path,
// tile cache and associated images on the repo's CMU-1-Small-Region.svs
// (path in FASTSLIDE_JPEG_TEST_SVS).

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fastslide/jpeg_decode_mode.h"
#include "fastslide/readers/aperio/aperio.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/runtime/lru_tile_cache.h"
#include "fastslide/slide_reader.h"

#if !defined(FASTSLIDE_JPEG_BACKEND_JPGD)
// Needed on gcc-11 before <jpeglib.h>, which uses the global FILE.
#include <stdio.h>

#include <jpeglib.h>
#endif

namespace fastslide {
namespace {

std::vector<uint8_t> Pixels(const Image& image) {
  return std::vector<uint8_t>(image.GetData(),
                              image.GetData() + image.SizeBytes());
}

std::unique_ptr<AperioReader> OpenTestSvs() {
  const char* path = std::getenv("FASTSLIDE_JPEG_TEST_SVS");
  if (path == nullptr) {
    return nullptr;
  }
  auto reader_or = AperioReader::Create(path);
  EXPECT_TRUE(reader_or.ok()) << reader_or.status().ToString();
  return reader_or.ok() ? std::move(*reader_or) : nullptr;
}

// A region spanning several tiles, so every decode path is exercised.
std::vector<uint8_t> ReadTestRegion(const SlideReader& reader) {
  RegionSpec region{};
  region.top_left = {0, 0};
  region.size = {512, 512};
  region.level = 0;
  auto image_or = reader.ReadRegion(region);
  EXPECT_TRUE(image_or.ok()) << image_or.status().ToString();
  return image_or.ok() ? Pixels(*image_or) : std::vector<uint8_t>{};
}

#define REQUIRE_TEST_SVS(reader)                          \
  if ((reader) == nullptr) {                              \
    GTEST_SKIP() << "FASTSLIDE_JPEG_TEST_SVS is not set"; \
  }

#if defined(FASTSLIDE_JPEG_BACKEND_JPGD)

TEST(JpegDecodeModeTest, JpgdRejectsLibjpegMode) {
  EXPECT_FALSE(
      runtime::decoders::JpegDecodeModeSupported(JpegDecodeMode::kLibjpeg));
  auto reader = OpenTestSvs();
  REQUIRE_TEST_SVS(reader);
  const auto status = reader->SetJpegDecodeMode(JpegDecodeMode::kLibjpeg);
  EXPECT_EQ(status.code(), aifocore::StatusCode::kUnimplemented);
  EXPECT_EQ(reader->GetJpegDecodeMode(), JpegDecodeMode::kFast);
}

#else

constexpr int kWidth = 64;
constexpr int kHeight = 48;

// YCbCr 4:2:0, where both IDCT and chroma upsampling change the output.
std::vector<uint8_t> EncodeTestJpeg() {
  std::vector<uint8_t> rgb(static_cast<size_t>(kWidth) * kHeight * 3);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      uint8_t* px = &rgb[(static_cast<size_t>(y) * kWidth + x) * 3];
      px[0] = static_cast<uint8_t>(x * 4);
      px[1] = static_cast<uint8_t>(y * 5);
      px[2] = static_cast<uint8_t>(((x ^ y) & 7) * 32);
    }
  }
  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  unsigned char* out = nullptr;
  unsigned long out_size = 0;  // NOLINT(runtime/int): libjpeg API type.
  jpeg_mem_dest(&cinfo, &out, &out_size);
  cinfo.image_width = kWidth;
  cinfo.image_height = kHeight;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 75, TRUE);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = &rgb[static_cast<size_t>(cinfo.next_scanline) * kWidth * 3];
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  std::vector<uint8_t> jpeg(out, out + out_size);
  jpeg_destroy_compress(&cinfo);
  free(out);
  return jpeg;
}

// Decode with libjpeg's defaults, as libtiff and OpenSlide do.
std::vector<uint8_t> ReferenceDecode(const std::vector<uint8_t>& jpeg) {
  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, const_cast<uint8_t*>(jpeg.data()), jpeg.size());
  jpeg_read_header(&cinfo, TRUE);
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);
  std::vector<uint8_t> rgb(static_cast<size_t>(cinfo.output_width) *
                           cinfo.output_height * 3);
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row = &rgb[static_cast<size_t>(cinfo.output_scanline) *
                        cinfo.output_width * 3];
    jpeg_read_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return rgb;
}

std::vector<uint8_t> DecodeWith(const std::vector<uint8_t>& jpeg,
                                JpegDecodeMode mode) {
  auto decoded_or = runtime::decoders::DecodeJpegToRgb(jpeg, {.mode = mode});
  EXPECT_TRUE(decoded_or.ok()) << decoded_or.status().ToString();
  return decoded_or.ok() ? decoded_or->rgb : std::vector<uint8_t>{};
}

TEST(JpegDecodeModeTest, DecoderLibjpegModeMatchesLibjpegDefaults) {
  EXPECT_TRUE(
      runtime::decoders::JpegDecodeModeSupported(JpegDecodeMode::kLibjpeg));
  const auto jpeg = EncodeTestJpeg();
  const auto reference = ReferenceDecode(jpeg);
  EXPECT_EQ(DecodeWith(jpeg, JpegDecodeMode::kLibjpeg), reference);
  EXPECT_NE(DecodeWith(jpeg, JpegDecodeMode::kFast), reference);
}

// The decoder reuses one decompressor per thread; switching modes must not
// leave the previous decode's settings behind.
TEST(JpegDecodeModeTest, DecoderSwitchingModesOnOneThread) {
  const auto jpeg = EncodeTestJpeg();
  const auto fast = DecodeWith(jpeg, JpegDecodeMode::kFast);
  const auto reference = ReferenceDecode(jpeg);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(DecodeWith(jpeg, JpegDecodeMode::kLibjpeg), reference);
    EXPECT_EQ(DecodeWith(jpeg, JpegDecodeMode::kFast), fast);
  }
}

TEST(JpegDecodeModeTest, ReaderDefaultsToFast) {
  auto reader = OpenTestSvs();
  REQUIRE_TEST_SVS(reader);
  EXPECT_EQ(reader->GetJpegDecodeMode(), JpegDecodeMode::kFast);
  const auto fast = ReadTestRegion(*reader);
  ASSERT_TRUE(reader->SetJpegDecodeMode(JpegDecodeMode::kLibjpeg).ok());
  EXPECT_EQ(reader->GetJpegDecodeMode(), JpegDecodeMode::kLibjpeg);
  const auto libjpeg = ReadTestRegion(*reader);
  ASSERT_EQ(fast.size(), libjpeg.size());
  EXPECT_NE(fast, libjpeg);
  ASSERT_TRUE(reader->SetJpegDecodeMode(JpegDecodeMode::kFast).ok());
  EXPECT_EQ(ReadTestRegion(*reader), fast);
}

// Without a cache, alternate reads of two readers in different modes. The
// decode threads keep one simpletiff context each, shared by both readers.
TEST(JpegDecodeModeTest, NoStaleThreadState) {
  auto fast_reader = OpenTestSvs();
  REQUIRE_TEST_SVS(fast_reader);
  auto libjpeg_reader = OpenTestSvs();
  ASSERT_TRUE(libjpeg_reader->SetJpegDecodeMode(JpegDecodeMode::kLibjpeg).ok());
  const auto fast = ReadTestRegion(*fast_reader);
  const auto libjpeg = ReadTestRegion(*libjpeg_reader);
  ASSERT_NE(fast, libjpeg);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(ReadTestRegion(*fast_reader), fast);
    EXPECT_EQ(ReadTestRegion(*libjpeg_reader), libjpeg);
  }
}

TEST(JpegDecodeModeTest, SharedCacheKeepsModesApart) {
  auto fast_reader = OpenTestSvs();
  REQUIRE_TEST_SVS(fast_reader);
  auto libjpeg_reader = OpenTestSvs();
  ASSERT_TRUE(libjpeg_reader->SetJpegDecodeMode(JpegDecodeMode::kLibjpeg).ok());
  const auto fast = ReadTestRegion(*fast_reader);
  const auto libjpeg = ReadTestRegion(*libjpeg_reader);

  auto cache = std::make_shared<runtime::LRUTileCache>(64 << 20);
  fast_reader->SetCache(cache);
  libjpeg_reader->SetCache(cache);
  // Fill the cache from one mode, then read with the other.
  EXPECT_EQ(ReadTestRegion(*fast_reader), fast);
  EXPECT_EQ(ReadTestRegion(*libjpeg_reader), libjpeg);
  EXPECT_EQ(ReadTestRegion(*fast_reader), fast);
  EXPECT_EQ(ReadTestRegion(*libjpeg_reader), libjpeg);
}

TEST(JpegDecodeModeTest, AssociatedImagesFollowMode) {
  auto fast_reader = OpenTestSvs();
  REQUIRE_TEST_SVS(fast_reader);
  auto libjpeg_reader = OpenTestSvs();
  ASSERT_TRUE(libjpeg_reader->SetJpegDecodeMode(JpegDecodeMode::kLibjpeg).ok());
  // Aperio's thumbnail and macro are JPEG; the label is usually LZW.
  int differing = 0;
  for (const auto& name : fast_reader->GetAssociatedImageNames()) {
    auto fast_or = fast_reader->ReadAssociatedImage(name);
    auto libjpeg_or = libjpeg_reader->ReadAssociatedImage(name);
    ASSERT_TRUE(fast_or.ok()) << fast_or.status().ToString();
    ASSERT_TRUE(libjpeg_or.ok()) << libjpeg_or.status().ToString();
    ASSERT_EQ(fast_or->SizeBytes(), libjpeg_or->SizeBytes()) << name;
    differing += Pixels(*fast_or) != Pixels(*libjpeg_or) ? 1 : 0;
  }
  EXPECT_GT(differing, 0);
}

#endif  // FASTSLIDE_JPEG_BACKEND_JPGD

}  // namespace
}  // namespace fastslide
