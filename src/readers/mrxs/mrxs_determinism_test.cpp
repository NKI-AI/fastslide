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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "fastslide/readers/mrxs/mrxs.h"

namespace fastslide {
namespace {

// Guarded end-to-end test. Set FASTSLIDE_MRXS_TEST_FILE to the path of a real
// MRXS slide. Skipped (not failed) when the variable is unset or the file is
// missing, so the suite stays hermetic in CI.
//
// MRXS camera tiles overlap; the same read must return the same pixels every
// time, whatever order the tiles finish decoding in.
TEST(MrxsDeterminismTest, RepeatedReadsReturnTheSamePixels) {
  const char* path = std::getenv("FASTSLIDE_MRXS_TEST_FILE");
  if (path == nullptr || !std::filesystem::exists(path)) {
    GTEST_SKIP()
        << "Set FASTSLIDE_MRXS_TEST_FILE to a .mrxs slide to run this.";
  }

  auto reader_or = MrxsReader::Create(path);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  auto& reader = *reader_or;

  const int level_count = reader->GetLevelCount();
  ASSERT_GT(level_count, 0);
  for (const int level : {0, level_count / 3, (2 * level_count) / 3}) {
    auto info_or = reader->GetLevelInfo(level);
    ASSERT_TRUE(info_or.ok());
    const auto dims = info_or->dimensions;
    // A region in the middle of the level, where camera tiles overlap.
    const uint32_t w = std::min<uint32_t>(512, dims[0]);
    const uint32_t h = std::min<uint32_t>(512, dims[1]);
    RegionSpec region;
    region.top_left = ImageCoordinate{(dims[0] - w) / 2, (dims[1] - h) / 2};
    region.size = ImageDimensions{w, h};
    region.level = level;

    auto first_or = reader->ReadRegion(region);
    ASSERT_TRUE(first_or.ok()) << first_or.status().message();
    const std::vector<uint8_t> first = first_or->GetDataVector();
    for (int i = 0; i < 10; ++i) {
      auto again_or = reader->ReadRegion(region);
      ASSERT_TRUE(again_or.ok()) << again_or.status().message();
      EXPECT_EQ(again_or->GetDataVector(), first)
          << "level " << level << ", read " << i + 1;
    }
  }
}

}  // namespace
}  // namespace fastslide
