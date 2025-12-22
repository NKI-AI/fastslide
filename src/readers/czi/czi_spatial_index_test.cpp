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

#include "fastslide/readers/czi/czi_spatial_index.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace fastslide::czi {
namespace {

TEST(CziSpatialIndexTest, QueryFindsFractionalTile) {
  SpatialTile tile{};
  tile.info.subblock_index = 7;
  tile.info.width = 10;
  tile.info.height = 10;
  tile.bbox.min = {0.5, 0.25};
  tile.bbox.max = {10.5, 10.25};

  auto index_or = CziSpatialIndex::Build(std::vector<SpatialTile>{tile}, 16.0);
  ASSERT_TRUE(index_or.ok()) << index_or.status().ToString();

  auto idxs = (*index_or)->QueryRegion(0.0, 0.0, 1.0, 1.0);
  ASSERT_EQ(idxs.size(), 1u);
  EXPECT_EQ(idxs[0], 0u);
}

TEST(CziSpatialIndexTest, QueryExcludesNonIntersectingTile) {
  SpatialTile tile{};
  tile.info.subblock_index = 1;
  tile.info.width = 10;
  tile.info.height = 10;
  tile.bbox.min = {100.0, 100.0};
  tile.bbox.max = {110.0, 110.0};

  auto index_or = CziSpatialIndex::Build(std::vector<SpatialTile>{tile}, 16.0);
  ASSERT_TRUE(index_or.ok()) << index_or.status().ToString();

  auto idxs = (*index_or)->QueryRegion(0.0, 0.0, 32.0, 32.0);
  EXPECT_TRUE(idxs.empty());
}

}  // namespace
}  // namespace fastslide::czi
