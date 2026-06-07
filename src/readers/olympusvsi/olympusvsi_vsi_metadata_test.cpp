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

#include "fastslide/readers/olympusvsi/olympusvsi_vsi_metadata.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace fastslide::formats::olympusvsi {
namespace {

// Tag / type constants mirrored from the on-disk Olympus layout.
constexpr int32_t kTagChannelName = 2419;
constexpr int32_t kTagImageBoundary = 2053;
constexpr int32_t kTagImageFrameVolume = 2002;
constexpr int32_t kTagExternalFileProperties = 2018;
constexpr int32_t kTagFrameScale = 2019;
constexpr int32_t kTagDeviceName = 34;
constexpr uint32_t kTypeBgr = 270;
constexpr uint32_t kTypeIntRect = 259;
constexpr uint32_t kTypeDouble2 = 260;
constexpr uint32_t kTypeUnicodeTChar = 8192;

// Dimension-ordering on-disk constants (mirrored from the reader).
constexpr int32_t kTagDimensionDescription = 2007;
constexpr int32_t kTagDimensionMeaning = 2023;
constexpr uint32_t kFlagExtraTag = 0x08000000u;
constexpr uint32_t kFlagInlineData = 0x40000000u;
constexpr uint32_t kAxisFocal = 1;    // Z.
constexpr uint32_t kAxisTime = 2;     // T.
constexpr uint32_t kAxisChannel = 4;  // C.

template <typename T>
void Append(std::vector<uint8_t>* out, T value) {
  const size_t off = out->size();
  out->resize(off + sizeof(T));
  std::memcpy(out->data() + off, &value, sizeof(T));
}

template <typename T>
void WriteAt(std::vector<uint8_t>* out, size_t offset, T value) {
  std::memcpy(out->data() + offset, &value, sizeof(T));
}

std::vector<uint8_t> IntRect(int32_t x, int32_t y, int32_t w, int32_t h) {
  std::vector<uint8_t> out;
  for (int32_t v : {x, y, w, h}) {
    Append(&out, v);
  }
  return out;
}

std::vector<uint8_t> Double2(double x, double y) {
  std::vector<uint8_t> out;
  Append(&out, x);
  Append(&out, y);
  return out;
}

std::vector<uint8_t> Utf16Le(const std::string& s) {
  std::vector<uint8_t> out;
  for (char c : s) {
    out.push_back(static_cast<uint8_t>(c));
    out.push_back(0);
  }
  return out;
}

// Builds a single 768-byte BGR display LUT whose endpoint (index 255) is
// the supplied colour, so the parser recovers (r, g, b).
std::vector<uint8_t> LutWithEndpoint(uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> lut(256 * 3, 0);
  lut[255 * 3 + 0] = b;  // BGR order on disk.
  lut[255 * 3 + 1] = g;
  lut[255 * 3 + 2] = r;
  return lut;
}

// Incrementally builds an Olympus tag-tree volume in memory.
//
// Layout: 8 bytes of TIFF-header filler (the parser seeks to 8), then a
// 24-byte volume header, then a flat list of data fields linked by their
// next-sibling offsets (relative to the volume start at byte 8).
class VolumeBuilder {
 public:
  VolumeBuilder() {
    buf_.resize(8, 0);  // TIFF header filler.
    volume_start_ = buf_.size();
    Append(&buf_, static_cast<uint16_t>(24));     // header size.
    Append(&buf_, static_cast<uint16_t>(21321));  // version.
    Append(&buf_, static_cast<uint32_t>(0));      // reserved.
    data_field_offset_slot_ = buf_.size();
    Append(&buf_, static_cast<int64_t>(0));  // data-field offset (patched).
    flags_slot_ = buf_.size();
    Append(&buf_, static_cast<uint32_t>(0));  // flags (tag count, patched).
    Append(&buf_, static_cast<uint32_t>(0));  // reserved.
  }

  // Adds a leaf field with no payload (used for structural markers like
  // the frame-group marker (2002) / external-pixels marker (2018)).
  void AddMarker(int32_t tag) { AddField(/*type_word=*/0, tag, {}); }

  // Adds a leaf value field carrying ``payload`` bytes.
  void AddValue(uint32_t real_type, int32_t tag,
                const std::vector<uint8_t>& payload) {
    AddField(real_type, tag, payload);
  }

  // Adds a dimension-description block field carrying the axis'
  // coordinate-slot index on its secondary (extra) tag. Real files nest a
  // sub-volume here; the reader only needs the secondary tag, so a flat
  // field with the extra-tag flag is sufficient for the test.
  void AddDimensionBlock(int32_t dim_index) {
    AddField(kFlagExtraTag, kTagDimensionDescription, {},
             /*extra_tag=*/dim_index);
  }

  // Adds an inline field whose value lives in the size word (no payload),
  // used for the dimension-meaning leaf.
  void AddInlineValue(int32_t tag, uint32_t value) {
    AddField(kFlagInlineData, tag, {}, /*extra_tag=*/std::nullopt,
             /*inline_size=*/value);
  }

  std::vector<uint8_t> Finish() {
    // First field starts immediately after the 24-byte volume header.
    WriteAt(&buf_, data_field_offset_slot_,
            static_cast<int64_t>(first_field_offset_ - volume_start_));
    WriteAt(&buf_, flags_slot_, static_cast<uint32_t>(field_count_));
    // Terminate the chain.
    if (last_next_field_slot_ != 0) {
      WriteAt(&buf_, last_next_field_slot_, static_cast<uint32_t>(0));
    }
    return buf_;
  }

 private:
  void AddField(uint32_t type_word, int32_t tag,
                const std::vector<uint8_t>& payload,
                std::optional<int32_t> extra_tag = std::nullopt,
                std::optional<uint32_t> inline_size = std::nullopt) {
    const size_t field_start = buf_.size();
    if (field_count_ == 0) {
      first_field_offset_ = field_start;
    } else {
      // Link the previous field to this one.
      WriteAt(&buf_, last_next_field_slot_,
              static_cast<uint32_t>(field_start - volume_start_));
    }
    Append(&buf_, type_word);  // type word (flags | real type).
    Append(&buf_, tag);        // tag.
    last_next_field_slot_ = buf_.size();
    Append(&buf_, static_cast<uint32_t>(0));  // next-sibling offset (patch).
    // Size word: payload byte count, or the inline value when inline.
    Append(&buf_, inline_size.value_or(static_cast<uint32_t>(payload.size())));
    if (extra_tag.has_value()) {
      Append(&buf_, *extra_tag);  // secondary tag word.
    }
    for (uint8_t byte : payload) {
      buf_.push_back(byte);
    }
    ++field_count_;
  }

  std::vector<uint8_t> buf_;
  size_t volume_start_ = 0;
  size_t data_field_offset_slot_ = 0;
  size_t flags_slot_ = 0;
  size_t first_field_offset_ = 0;
  size_t last_next_field_slot_ = 0;
  uint32_t field_count_ = 0;
};

std::filesystem::path WriteTemp(const std::vector<uint8_t>& bytes,
                                const std::string& stem) {
  const auto path =
      std::filesystem::temp_directory_path() /
      (stem + "_" +
       std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
       ".vsi");
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  out.close();
  return path;
}

TEST(OlympusVsiVsiMetadata, ExtractsChannelNamesAndLutColors) {
  VolumeBuilder b;
  // Open pyramid 0: image-frame volume then external-file properties.
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  // Image boundary rectangle (x, y, width, height).
  b.AddValue(kTypeIntRect, kTagImageBoundary, IntRect(0, 0, 7230, 6151));
  // Three channels: LUT endpoint precedes each channel name.
  b.AddValue(kTypeBgr, /*tag=*/0, LutWithEndpoint(0, 0, 255));
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("FL DAPI"));
  b.AddValue(kTypeBgr, /*tag=*/0, LutWithEndpoint(31, 255, 0));
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("FL FITC"));
  b.AddValue(kTypeBgr, /*tag=*/0, LutWithEndpoint(255, 117, 0));
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("FL CY3"));

  const auto path = WriteTemp(b.Finish(), "vsi_meta_basic");
  const auto pyramids = ParseVsiMetadata(path).pyramids;
  std::filesystem::remove(path);

  ASSERT_FALSE(pyramids.empty());
  const auto& p0 = pyramids.front();
  EXPECT_EQ(p0.width, 7230U);
  EXPECT_EQ(p0.height, 6151U);
  ASSERT_EQ(p0.channels.size(), 3U);

  EXPECT_EQ(p0.channels[0].name, "FL DAPI");
  ASSERT_TRUE(p0.channels[0].color.has_value());
  EXPECT_EQ(p0.channels[0].color->r, 0);
  EXPECT_EQ(p0.channels[0].color->g, 0);
  EXPECT_EQ(p0.channels[0].color->b, 255);

  EXPECT_EQ(p0.channels[1].name, "FL FITC");
  ASSERT_TRUE(p0.channels[1].color.has_value());
  EXPECT_EQ(p0.channels[1].color->r, 31);
  EXPECT_EQ(p0.channels[1].color->g, 255);
  EXPECT_EQ(p0.channels[1].color->b, 0);

  EXPECT_EQ(p0.channels[2].name, "FL CY3");
  ASSERT_TRUE(p0.channels[2].color.has_value());
  EXPECT_EQ(p0.channels[2].color->r, 255);
  EXPECT_EQ(p0.channels[2].color->g, 117);
  EXPECT_EQ(p0.channels[2].color->b, 0);
}

TEST(OlympusVsiVsiMetadata, SeparatesChannelsAcrossPyramids) {
  VolumeBuilder b;
  // Pyramid 0.
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("Ch A"));
  // Pyramid 1.
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("Ch B"));
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("Ch C"));

  const auto path = WriteTemp(b.Finish(), "vsi_meta_split");
  const auto pyramids = ParseVsiMetadata(path).pyramids;
  std::filesystem::remove(path);

  ASSERT_EQ(pyramids.size(), 2U);
  ASSERT_EQ(pyramids[0].channels.size(), 1U);
  EXPECT_EQ(pyramids[0].channels[0].name, "Ch A");
  EXPECT_FALSE(pyramids[0].channels[0].color.has_value());
  ASSERT_EQ(pyramids[1].channels.size(), 2U);
  EXPECT_EQ(pyramids[1].channels[0].name, "Ch B");
  EXPECT_EQ(pyramids[1].channels[1].name, "Ch C");
}

TEST(OlympusVsiVsiMetadata, ResolvesDimensionAxisSlots) {
  VolumeBuilder b;
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  // Three dimension-description blocks declaring, in tile-coordinate slot
  // order: slot 2 -> time, slot 3 -> focal (Z), slot 4 -> channel. Each
  // block's secondary tag is the dimension index (slot - 2).
  b.AddDimensionBlock(/*dim_index=*/0);
  b.AddInlineValue(kTagDimensionMeaning, kAxisTime);
  b.AddDimensionBlock(/*dim_index=*/1);
  b.AddInlineValue(kTagDimensionMeaning, kAxisFocal);
  b.AddDimensionBlock(/*dim_index=*/2);
  b.AddInlineValue(kTagDimensionMeaning, kAxisChannel);
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("C405"));
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("C488"));

  const auto path = WriteTemp(b.Finish(), "vsi_meta_dims");
  const auto pyramids = ParseVsiMetadata(path).pyramids;
  std::filesystem::remove(path);

  ASSERT_EQ(pyramids.size(), 1U);
  const auto& p0 = pyramids.front();
  EXPECT_EQ(p0.time_slot, 2);
  EXPECT_EQ(p0.focal_slot, 3);
  EXPECT_EQ(p0.channel_slot, 4);
  ASSERT_EQ(p0.channels.size(), 2U);
  EXPECT_EQ(p0.channels[0].name, "C405");
  EXPECT_EQ(p0.channels[1].name, "C488");
}

TEST(OlympusVsiVsiMetadata, DefaultsDimensionSlotsWhenAbsent) {
  VolumeBuilder b;
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("Ch A"));

  const auto path = WriteTemp(b.Finish(), "vsi_meta_nodims");
  const auto pyramids = ParseVsiMetadata(path).pyramids;
  std::filesystem::remove(path);

  ASSERT_EQ(pyramids.size(), 1U);
  EXPECT_EQ(pyramids[0].channel_slot, -1);
  EXPECT_EQ(pyramids[0].focal_slot, -1);
  EXPECT_EQ(pyramids[0].time_slot, -1);
}

TEST(OlympusVsiVsiMetadata, ExtractsPerImageFrameScale) {
  VolumeBuilder b;
  // Pyramid 0: a fine scan scale (micron-scale tag 2019 before the boundary).
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  b.AddValue(kTypeDouble2, kTagFrameScale, Double2(0.3528, 0.3528));
  b.AddValue(kTypeIntRect, kTagImageBoundary, IntRect(0, 0, 7230, 6151));
  // Pyramid 1: a coarser overview scale -> a different per-image mpp.
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  b.AddValue(kTypeDouble2, kTagFrameScale, Double2(4.0, 2.0));

  const auto path = WriteTemp(b.Finish(), "vsi_meta_scale");
  const auto pyramids = ParseVsiMetadata(path).pyramids;
  std::filesystem::remove(path);

  ASSERT_EQ(pyramids.size(), 2U);
  EXPECT_DOUBLE_EQ(pyramids[0].mpp_x, 0.3528);
  EXPECT_DOUBLE_EQ(pyramids[0].mpp_y, 0.3528);
  EXPECT_DOUBLE_EQ(pyramids[1].mpp_x, 4.0);
  EXPECT_DOUBLE_EQ(pyramids[1].mpp_y, 2.0);
}

TEST(OlympusVsiVsiMetadata, ExtractsDeviceName) {
  VolumeBuilder b;
  // One image frame so the file is a plausible container.
  b.AddMarker(kTagImageFrameVolume);
  b.AddMarker(kTagExternalFileProperties);
  b.AddValue(kTypeUnicodeTChar, kTagChannelName, Utf16Le("DAPI"));
  // Document-scope device name (closes the image group first).
  b.AddMarker(/*tag=*/2109);  // Document-scope closer.
  b.AddValue(kTypeUnicodeTChar, kTagDeviceName, Utf16Le("OLYMPUS VS200 ASW"));

  const auto path = WriteTemp(b.Finish(), "vsi_meta_device");
  const auto meta = ParseVsiMetadata(path);
  std::filesystem::remove(path);

  EXPECT_EQ(meta.device_name, "OLYMPUS VS200 ASW");
}

TEST(OlympusVsiVsiMetadata, MissingFileYieldsEmpty) {
  const auto meta = ParseVsiMetadata(std::filesystem::temp_directory_path() /
                                     "does_not_exist_42.vsi");
  EXPECT_TRUE(meta.pyramids.empty());
  EXPECT_TRUE(meta.device_name.empty());
}

}  // namespace
}  // namespace fastslide::formats::olympusvsi
