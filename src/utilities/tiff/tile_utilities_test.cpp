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

#include "fastslide/utilities/tiff/tile_utilities.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "aifocore/platform/portability.h"
#include "gtest/gtest.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide::tiff {
namespace {

void AppendLe16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

void AppendLe32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
}

void AppendEntry(std::vector<uint8_t>& out, uint16_t tag, uint16_t type,
                 uint32_t count, uint32_t value) {
  AppendLe16(out, tag);
  AppendLe16(out, type);
  AppendLe32(out, count);
  AppendLe32(out, value);
}

// Writes a classic little-endian TIFF of one 4x4 gray strip page, with `icc`
// as its ICC profile (tag 34675) unless it is empty.
std::filesystem::path WriteTiff(const std::string& name,
                                const std::string& icc) {
  constexpr uint16_t kShort = 3;
  constexpr uint16_t kLong = 4;
  constexpr uint16_t kUndefined = 7;
  constexpr uint32_t kSide = 4;
  constexpr uint32_t kIfdOffset = 8;
  const uint16_t entries = icc.empty() ? 9 : 10;
  const uint32_t icc_offset = kIfdOffset + 2 + entries * 12 + 4;
  const uint32_t strip_offset = icc_offset + static_cast<uint32_t>(icc.size());

  std::vector<uint8_t> tiff = {'I', 'I'};
  AppendLe16(tiff, 42);
  AppendLe32(tiff, kIfdOffset);
  AppendLe16(tiff, entries);
  AppendEntry(tiff, 256, kLong, 1, kSide);
  AppendEntry(tiff, 257, kLong, 1, kSide);
  AppendEntry(tiff, 258, kShort, 1, 8);
  AppendEntry(tiff, 259, kShort, 1, 1);
  AppendEntry(tiff, 262, kShort, 1, 1);
  AppendEntry(tiff, 273, kLong, 1, strip_offset);
  AppendEntry(tiff, 277, kShort, 1, 1);
  AppendEntry(tiff, 278, kLong, 1, kSide);
  AppendEntry(tiff, 279, kLong, 1, kSide * kSide);
  if (!icc.empty()) {
    AppendEntry(tiff, 34675, kUndefined, static_cast<uint32_t>(icc.size()),
                icc_offset);
  }
  AppendLe32(tiff, 0);
  tiff.insert(tiff.end(), icc.begin(), icc.end());
  tiff.insert(tiff.end(), kSide * kSide, 0);

  const char* tmpdir = std::getenv("TEST_TMPDIR");
  const std::filesystem::path dir =
      tmpdir ? std::filesystem::path(tmpdir)
             : std::filesystem::temp_directory_path();
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path path = dir / (name + "_" + unique + ".tif");
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(tiff.data()),
             static_cast<std::streamsize>(tiff.size()));
  return path;
}

TEST(ExtractIccProfileTest, ReadsTheProfileFromTheFile) {
  std::string icc(300, '\0');
  for (size_t i = 0; i < icc.size(); ++i) {
    icc[i] = static_cast<char>(i % 5 == 0 ? 0 : i);
  }
  const auto path = WriteTiff("fastslide_icc", icc);
  simpletiff::TiffIndex index;
  int fd = -1;
  ASSERT_TRUE(simpletiff::OpenTiff(path.string(), index, fd));

  auto profile = ExtractIccProfile(index);
  ASSERT_TRUE(profile.ok());
  EXPECT_EQ(std::string(profile->begin(), profile->end()), icc);

  if (fd >= 0) {
    aifocore::portable_close(fd);
  }
}

TEST(ExtractIccProfileTest, NoProfileIsNotFound) {
  const auto path = WriteTiff("fastslide_no_icc", "");
  simpletiff::TiffIndex index;
  int fd = -1;
  ASSERT_TRUE(simpletiff::OpenTiff(path.string(), index, fd));

  auto profile = ExtractIccProfile(index);
  EXPECT_FALSE(profile.ok());
  EXPECT_EQ(profile.status().code(), aifocore::StatusCode::kNotFound);

  if (fd >= 0) {
    aifocore::portable_close(fd);
  }
}

}  // namespace
}  // namespace fastslide::tiff
