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

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/dicom/dicom.h"

namespace fastslide {
namespace {

namespace fs = std::filesystem;

/// @brief Locate the directory holding the synthetic concatenation fixtures.
///
/// Resolution order:
///   1. `FASTSLIDE_DICOM_TESTDATA_DIR` env var (overrides for ad-hoc runs).
///   2. Bazel test runfiles. Under bzlmod the data file lives at
///        `$TEST_SRCDIR/<fastslide canonical repo>/src/readers/dicom/testdata`
///      where the canonical repo name is `_main` when fastslide IS the root
///      module (standalone build) and `fastslide+` when loaded as an external
///      dep (e.g. the aifo monorepo's `local_path_override`). The
///      `BAZEL_CURRENT_REPOSITORY` macro is auto-injected by Bazel for every
///      C++ compilation unit and resolves to exactly that canonical name
///      (empty string for the root module). `TEST_WORKSPACE` alone is *not*
///      sufficient: it tracks the consumer's main workspace, which stays
///      `_main` even when the test target itself lives in an external repo.
///   3. Workspace-relative path from the current working directory.
fs::path LocateTestdataDir() {
  // Package-relative path of the testdata directory inside the runfiles tree.
  static constexpr const char* kRepoRelative = "src/readers/dicom/testdata";

  if (const char* override_dir = std::getenv("FASTSLIDE_DICOM_TESTDATA_DIR")) {
    return fs::path(override_dir);
  }

  const char* srcdir = std::getenv("TEST_SRCDIR");
  if (srcdir != nullptr) {
#ifdef BAZEL_CURRENT_REPOSITORY
    std::string repo = BAZEL_CURRENT_REPOSITORY;
#else
    std::string repo;
#endif
    if (repo.empty()) {
      // Root module: `TEST_WORKSPACE` (e.g. `_main`) is the right slot.
      if (const char* workspace = std::getenv("TEST_WORKSPACE")) {
        repo = workspace;
      }
    }
    if (!repo.empty()) {
      return fs::path(srcdir) / repo / kRepoRelative;
    }
  }

  return fs::path(kRepoRelative);
}

class DicomConcatenationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    testdata_dir_ = LocateTestdataDir();
    ASSERT_TRUE(fs::exists(testdata_dir_))
        << "Missing test fixture directory: " << testdata_dir_;
    ASSERT_TRUE(fs::exists(testdata_dir_ / "part_1.dcm"));
    ASSERT_TRUE(fs::exists(testdata_dir_ / "part_2.dcm"));
  }

  fs::path testdata_dir_;
};

// The synthetic fixture encodes each frame as a uniform colour derived from
// its global frame index. Mirrors generate_concatenation.py.
std::array<uint8_t, 3> ExpectedColorForFrame(uint32_t global_frame) {
  return {
      static_cast<uint8_t>((global_frame * 31) & 0xFF),
      static_cast<uint8_t>((global_frame * 61) & 0xFF),
      static_cast<uint8_t>((global_frame * 97) & 0xFF),
  };
}

TEST_F(DicomConcatenationTest, OpensConcatenationDirectoryAsSingleLevel) {
  auto reader_or = DicomReader::Create(testdata_dir_.string());
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().ToString();
  auto reader = std::move(*reader_or);

  EXPECT_EQ(reader->GetLevelCount(), 1);

  auto info_or = reader->GetLevelInfo(0);
  ASSERT_TRUE(info_or.ok()) << info_or.status().ToString();
  EXPECT_EQ(info_or->dimensions[0], 64u);
  EXPECT_EQ(info_or->dimensions[1], 32u);
  EXPECT_DOUBLE_EQ(info_or->downsample_factor, 1.0);

  const auto tile_size = reader->GetTileSize();
  EXPECT_EQ(tile_size[0], 16u);
  EXPECT_EQ(tile_size[1], 16u);
}

TEST_F(DicomConcatenationTest, MergesBothPartsIntoOneLevel) {
  auto reader_or = DicomReader::Create(testdata_dir_.string());
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().ToString();
  auto reader = std::move(*reader_or);

  // Both files share the same SeriesInstanceUID so they collapse to one
  // logical slide; the level itself must be backed by both parts.
  const auto& level = reader->GetLevel(0);
  ASSERT_EQ(level.parts.size(), 2u);
  EXPECT_FALSE(level.PrimaryFile().concatenation_uid.empty());

  const auto& part_a = *level.parts[0].file;
  const auto& part_b = *level.parts[1].file;
  EXPECT_EQ(part_a.concatenation_uid, part_b.concatenation_uid);
  EXPECT_NE(part_a.sop_instance_uid, part_b.sop_instance_uid);
  EXPECT_NE(part_a.in_concatenation_number, part_b.in_concatenation_number);

  // Parts are stored sorted by frame_offset; the union must cover all
  // 8 frames with no gaps.
  EXPECT_EQ(level.parts[0].frame_offset, 0u);
  EXPECT_EQ(level.parts[0].frame_offset + level.parts[0].frame_count,
            level.parts[1].frame_offset);
  EXPECT_EQ(level.parts[1].frame_offset + level.parts[1].frame_count, 8u);
}

TEST_F(DicomConcatenationTest, FindPartForFrameSpansAllFrames) {
  auto reader_or = DicomReader::Create(testdata_dir_.string());
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().ToString();
  auto reader = std::move(*reader_or);

  const auto& level = reader->GetLevel(0);
  for (uint32_t frame_index = 0; frame_index < 8; ++frame_index) {
    const DicomLevelPart* part = level.FindPartForFrame(frame_index);
    ASSERT_NE(part, nullptr) << "No part for frame " << frame_index;
    EXPECT_LE(part->frame_offset, frame_index);
    EXPECT_LT(frame_index, part->frame_offset + part->frame_count);
  }
  EXPECT_EQ(level.FindPartForFrame(8u), nullptr);
}

TEST_F(DicomConcatenationTest, ReadsTilesFromBothParts) {
  auto reader_or = DicomReader::Create(testdata_dir_.string());
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().ToString();
  auto reader = std::move(*reader_or);

  // Read each 16x16 tile of the level. Tiles 0..3 (top row) belong to
  // part 1; tiles 4..7 (bottom row) belong to part 2. The reader must
  // route each request to the correct file and return the expected
  // uniform colour.
  for (uint32_t row = 0; row < 2; ++row) {
    for (uint32_t col = 0; col < 4; ++col) {
      const uint32_t frame_index = row * 4 + col;

      core::RegionSpec region;
      region.top_left = {col * 16, row * 16};
      region.size = {16, 16};
      region.level = 0;

      auto image_or = reader->ReadRegion(region);
      ASSERT_TRUE(image_or.ok())
          << "frame " << frame_index << ": " << image_or.status().ToString();

      const auto& image = *image_or;
      ASSERT_EQ(image.GetDataType(), DataType::kUInt8);
      ASSERT_EQ(image.GetChannels(), 3u);
      const auto dims = image.GetDimensions();
      ASSERT_EQ(dims[0], 16u);
      ASSERT_EQ(dims[1], 16u);

      const uint8_t* pixels = image.GetData();
      ASSERT_NE(pixels, nullptr);
      const auto expected = ExpectedColorForFrame(frame_index);

      for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
          const uint8_t* px = pixels + ((y * 16) + x) * 3;
          EXPECT_EQ(px[0], expected[0])
              << "frame " << frame_index << " pixel (" << x << "," << y << ")";
          EXPECT_EQ(px[1], expected[1])
              << "frame " << frame_index << " pixel (" << x << "," << y << ")";
          EXPECT_EQ(px[2], expected[2])
              << "frame " << frame_index << " pixel (" << x << "," << y << ")";
        }
      }
    }
  }
}

}  // namespace
}  // namespace fastslide
