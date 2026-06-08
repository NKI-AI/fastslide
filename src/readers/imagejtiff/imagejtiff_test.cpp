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

#include "fastslide/readers/imagejtiff/imagejtiff.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/generictiff/generictiff_format_plugin.h"
#include "fastslide/runtime/format_descriptor.h"

namespace fastslide {
namespace {

// ---- Minimal little-endian classic TIFF writer for the tests ----------------

void AppendU16(std::vector<uint8_t>& buf, uint16_t value) {
  buf.push_back(static_cast<uint8_t>(value & 0xFF));
  buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void AppendU32(std::vector<uint8_t>& buf, uint32_t value) {
  buf.push_back(static_cast<uint8_t>(value & 0xFF));
  buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

struct IfdEntry {
  uint16_t tag;
  uint16_t type;  // 2=ASCII, 3=SHORT, 4=LONG
  uint32_t count;
  uint32_t value;  // inline value (SHORT/LONG) or byte offset (ASCII/array)
};

void AppendIfdEntry(std::vector<uint8_t>& buf, const IfdEntry& entry) {
  AppendU16(buf, entry.tag);
  AppendU16(buf, entry.type);
  AppendU32(buf, entry.count);
  if (entry.type == 3 && entry.count == 1) {
    // SHORT value is left-justified within the 4-byte value field.
    AppendU16(buf, static_cast<uint16_t>(entry.value));
    AppendU16(buf, 0);
  } else {
    AppendU32(buf, entry.value);
  }
}

/// @brief Build a 2-page, single-sample 32-bit ImageJ-style TIFF on disk.
///
/// Page 0 carries the ImageJ ImageDescription (channels=2). Page 0 pixels are
/// all `ch0` and page 1 pixels are all `ch1`, so the channels are distinct.
///
/// `sample_format` controls TIFF tag 339: pass 3 to declare IEEE float (a
/// well-formed ImageJ float file), or 0 to omit the tag entirely. Per the TIFF
/// spec an absent tag means unsigned integer, so a 32-bit page without it is
/// read as uint32 (matching tifffile).
void WriteImageJFloatTiff(const std::filesystem::path& path, uint16_t width,
                          uint16_t height, float ch0, float ch1,
                          uint16_t sample_format) {
  const std::string description =
      "ImageJ=1.54p\nimages=2\nchannels=2\nslices=1\nframes=1\nmode=color\n";
  // ASCII count includes the trailing NUL.
  const uint32_t desc_count = static_cast<uint32_t>(description.size() + 1);

  const uint32_t strip_bytes =
      static_cast<uint32_t>(width) * height * sizeof(float);

  // SampleFormat (tag 339) is appended last (ascending tag order) when set.
  const bool with_sample_format = sample_format != 0;
  const uint32_t n0 = with_sample_format ? 11 : 10;  // IFD0 entry count.
  const uint32_t n1 = with_sample_format ? 10 : 9;   // IFD1 entry count.

  // Layout: header | strip0 | strip1 | description | IFD0 | IFD1.
  const uint32_t strip0_offset = 8;
  const uint32_t strip1_offset = strip0_offset + strip_bytes;
  uint32_t desc_offset = strip1_offset + strip_bytes;
  if ((desc_offset & 1U) != 0) {
    ++desc_offset;  // Word-align.
  }
  uint32_t ifd0_offset = desc_offset + desc_count;
  if ((ifd0_offset & 1U) != 0) {
    ++ifd0_offset;
  }
  const uint32_t ifd0_size = 2 + 12 * n0 + 4;
  const uint32_t ifd1_offset = ifd0_offset + ifd0_size;

  std::vector<uint8_t> buf;
  buf.reserve(ifd1_offset + 2 + 12 * n1 + 4);

  // Header.
  buf.push_back('I');
  buf.push_back('I');
  AppendU16(buf, 42);
  AppendU32(buf, ifd0_offset);

  auto pad_to = [&buf](uint32_t target) {
    while (buf.size() < target) {
      buf.push_back(0);
    }
  };

  // Strip 0 (page 0 pixels).
  pad_to(strip0_offset);
  for (uint32_t i = 0; i < static_cast<uint32_t>(width) * height; ++i) {
    uint32_t bits = 0;
    std::memcpy(&bits, &ch0, sizeof(bits));
    AppendU32(buf, bits);
  }
  // Strip 1 (page 1 pixels).
  pad_to(strip1_offset);
  for (uint32_t i = 0; i < static_cast<uint32_t>(width) * height; ++i) {
    uint32_t bits = 0;
    std::memcpy(&bits, &ch1, sizeof(bits));
    AppendU32(buf, bits);
  }

  // Description bytes.
  pad_to(desc_offset);
  buf.insert(buf.end(), description.begin(), description.end());
  buf.push_back(0);  // NUL terminator.

  // IFD0 (page 0, with description).
  pad_to(ifd0_offset);
  AppendU16(buf, static_cast<uint16_t>(n0));  // entry count
  AppendIfdEntry(buf, {256, 3, 1, width});    // ImageWidth
  AppendIfdEntry(buf, {257, 3, 1, height});   // ImageLength
  AppendIfdEntry(buf, {258, 3, 1, 32});       // BitsPerSample
  AppendIfdEntry(buf, {259, 3, 1, 1});        // Compression = none
  AppendIfdEntry(buf, {262, 3, 1, 1});        // Photometric = min-is-black
  AppendIfdEntry(buf, {270, 2, desc_count, desc_offset});  // ImageDescription
  AppendIfdEntry(buf, {273, 4, 1, strip0_offset});         // StripOffsets
  AppendIfdEntry(buf, {277, 3, 1, 1});                     // SamplesPerPixel
  AppendIfdEntry(buf, {278, 3, 1, height});                // RowsPerStrip
  AppendIfdEntry(buf, {279, 4, 1, strip_bytes});           // StripByteCounts
  if (with_sample_format) {
    AppendIfdEntry(buf, {339, 3, 1, sample_format});  // SampleFormat
  }
  AppendU32(buf, ifd1_offset);  // next IFD

  // IFD1 (page 1, no description).
  pad_to(ifd1_offset);
  AppendU16(buf, static_cast<uint16_t>(n1));  // entry count
  AppendIfdEntry(buf, {256, 3, 1, width});
  AppendIfdEntry(buf, {257, 3, 1, height});
  AppendIfdEntry(buf, {258, 3, 1, 32});
  AppendIfdEntry(buf, {259, 3, 1, 1});
  AppendIfdEntry(buf, {262, 3, 1, 1});
  AppendIfdEntry(buf, {273, 4, 1, strip1_offset});
  AppendIfdEntry(buf, {277, 3, 1, 1});
  AppendIfdEntry(buf, {278, 3, 1, height});
  AppendIfdEntry(buf, {279, 4, 1, strip_bytes});
  if (with_sample_format) {
    AppendIfdEntry(buf, {339, 3, 1, sample_format});
  }
  AppendU32(buf, 0);  // no next IFD

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
  out.close();
}

class ImageJTiffTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            "fastslide_imagej_float_test.tif";
    // A well-formed ImageJ float file declares SampleFormat == 3 (IEEE float).
    WriteImageJFloatTiff(path_, kWidth, kHeight, kCh0, kCh1,
                         /*sample_format=*/3);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  static constexpr uint16_t kWidth = 4;
  static constexpr uint16_t kHeight = 3;
  static constexpr float kCh0 = 1.0F;
  static constexpr float kCh1 = 2.0F;

  std::filesystem::path path_;
};

TEST_F(ImageJTiffTest, ClassifiesAsSpectralFloat32MultiChannel) {
  auto reader_or = ImageJTiffReader::Create(path_);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  const auto& reader = *reader_or;

  EXPECT_EQ(reader->GetFormatName(), "ImageJ TIFF");
  EXPECT_EQ(reader->GetImageFormat(), ImageFormat::kSpectral);
  EXPECT_EQ(reader->GetDataType(), DataType::kFloat32);
  EXPECT_EQ(reader->GetChannelMetadata().size(), 2U);
  EXPECT_EQ(reader->GetLevelCount(), 1);
}

TEST(ImageJTiffSampleFormatTest, AbsentSampleFormatIsReadAsUInt32) {
  // A 32-bit ImageJ page that omits SampleFormat (tag 339) must be read as
  // unsigned integer per the TIFF spec - matching tifffile/libtiff - rather
  // than being assumed to be float. The ImageJ ImageDescription min/max are
  // display values and must not influence the data type.
  const auto path = std::filesystem::temp_directory_path() /
                    "fastslide_imagej_uint32_test.tif";
  WriteImageJFloatTiff(path, 4, 3, 1.0F, 2.0F, /*sample_format=*/0);

  auto reader_or = ImageJTiffReader::Create(path);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  const auto& reader = *reader_or;

  EXPECT_EQ(reader->GetImageFormat(), ImageFormat::kSpectral);
  EXPECT_EQ(reader->GetDataType(), DataType::kUInt32);
  EXPECT_EQ(reader->GetChannelMetadata().size(), 2U);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_F(ImageJTiffTest, FactoryRoutesImageJFilesToImageJReader) {
  const FormatDescriptor desc =
      formats::generictiff::CreateGenericTiffFormatDescriptor();
  auto reader_or = desc.factory(nullptr, path_.string());
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  EXPECT_EQ((*reader_or)->GetFormatName(), "ImageJ TIFF");
}

TEST_F(ImageJTiffTest, ReadsBothChannelsAsDistinctFloatPlanes) {
  auto reader_or = ImageJTiffReader::Create(path_);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  const auto& reader = *reader_or;

  RegionSpec region;
  region.top_left = {0, 0};
  region.size = {kWidth, kHeight};
  region.level = 0;

  auto image_or = reader->ReadRegion(region);
  ASSERT_TRUE(image_or.ok()) << image_or.status().message();
  const Image& image = *image_or;

  EXPECT_EQ(image.GetWidth(), kWidth);
  EXPECT_EQ(image.GetHeight(), kHeight);
  EXPECT_EQ(image.GetChannels(), 2U);
  EXPECT_EQ(image.GetDataType(), DataType::kFloat32);
  EXPECT_EQ(image.GetPlanarConfig(), PlanarConfig::kSeparate);

  const float* data = image.GetDataAs<float>();
  const size_t pixels = static_cast<size_t>(kWidth) * kHeight;
  for (size_t i = 0; i < pixels; ++i) {
    EXPECT_FLOAT_EQ(data[i], kCh0) << "channel 0 pixel " << i;
    EXPECT_FLOAT_EQ(data[pixels + i], kCh1) << "channel 1 pixel " << i;
  }
}

}  // namespace
}  // namespace fastslide
