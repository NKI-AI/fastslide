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

/// @file czi_scene_plane_test.cpp
/// @brief Z (focal) / T (time) plane selection in CziSceneImage.
///
/// Exercises the scene-level planning path with synthetic subblocks, so the
/// plane selector logic can be verified without a multi-Z/T CZI sample file.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "fastslide/core/tile_request.h"
#include "fastslide/readers/czi/czi.h"
#include "fastslide/readers/czi/czi_level_info.h"
#include "fastslide/readers/czi/czi_scene_image.h"

namespace fastslide {

/// @brief Test seam: build a `CziReader` with synthetic subblocks (no file
/// I/O).
struct CziReaderTestAccess {
  static std::unique_ptr<CziReader> MakeWithSubblocks(
      std::vector<CziSubblockInfo> subblocks) {
    std::unique_ptr<CziReader> reader(new CziReader("synthetic.czi"));
    reader->subblocks_ = std::move(subblocks);
    return reader;
  }
};

namespace {

CziSubblockInfo MakeTile(uint32_t index, int32_t z, int32_t t) {
  CziSubblockInfo sb;
  sb.index = index;
  sb.pixel_type = 0;  // Not decoded in these planning-only tests.
  sb.x = 0;
  sb.y = 0;
  sb.w = 256;
  sb.h = 256;
  sb.scene = 0;
  sb.z = z;
  sb.t = t;
  sb.downsample = 1;
  return sb;
}

// 2 Z planes x 2 T points, one 256x256 tile each, all at the scene origin.
std::vector<CziSubblockInfo> MakeMultiPlaneSubblocks() {
  return {
      MakeTile(0, /*z=*/0, /*t=*/0),
      MakeTile(1, /*z=*/0, /*t=*/1),
      MakeTile(2, /*z=*/1, /*t=*/0),
      MakeTile(3, /*z=*/1, /*t=*/1),
  };
}

// CziSceneImage owns a std::mutex and is therefore neither copyable nor
// movable, so it is always constructed in place behind a unique_ptr.
std::unique_ptr<CziSceneImage> MakeMultiPlaneScene(const CziReader& reader) {
  return std::make_unique<CziSceneImage>(
      reader, /*scene_id=*/0, "Scene 0",
      /*subblock_indices=*/std::vector<uint32_t>{0, 1, 2, 3}, /*mpp_x=*/0.25,
      /*mpp_y=*/0.25, /*objective_magnification=*/40.0, "Zeiss",
      /*z_spacing_um=*/2.0, /*t_interval_s=*/0.5);
}

core::TileRequest FullLevelRequest(int level, uint32_t z, uint32_t t) {
  core::TileRequest req;
  req.level = level;
  req.plane = {z, t};
  req.region_bounds = core::FractionalRegionBounds{
      .x = 0.0, .y = 0.0, .width = 256.0, .height = 256.0};
  return req;
}

TEST(CziScenePlaneTest, GetStackInfoReportsCountsAndSpacing) {
  auto reader =
      CziReaderTestAccess::MakeWithSubblocks(MakeMultiPlaneSubblocks());
  const auto scene = MakeMultiPlaneScene(*reader);

  const StackInfo info = scene->GetStackInfo();
  EXPECT_EQ(info.z_count, 2u);
  EXPECT_EQ(info.t_count, 2u);
  ASSERT_TRUE(info.z_spacing_um.has_value());
  EXPECT_DOUBLE_EQ(*info.z_spacing_um, 2.0);
  ASSERT_TRUE(info.t_interval_s.has_value());
  EXPECT_DOUBLE_EQ(*info.t_interval_s, 0.5);
}

TEST(CziScenePlaneTest, PrepareRequestSelectsOnlyRequestedPlane) {
  auto reader =
      CziReaderTestAccess::MakeWithSubblocks(MakeMultiPlaneSubblocks());
  const auto scene = MakeMultiPlaneScene(*reader);

  // z index 1 -> Z start 1; t index 0 -> T start 0. Only subblock 2 matches.
  auto plan_or = scene->PrepareRequest(FullLevelRequest(/*level=*/0, 1, 0));
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().ToString();
  ASSERT_EQ(plan_or->operations.size(), 1u);
  EXPECT_EQ(plan_or->operations[0].tile_coord.x, 2u);

  // The complementary plane resolves to its own, different subblock.
  auto other_or = scene->PrepareRequest(FullLevelRequest(/*level=*/0, 0, 1));
  ASSERT_TRUE(other_or.ok()) << other_or.status().ToString();
  ASSERT_EQ(other_or->operations.size(), 1u);
  EXPECT_EQ(other_or->operations[0].tile_coord.x, 1u);
}

TEST(CziScenePlaneTest, PrepareRequestRejectsOutOfRangePlane) {
  auto reader =
      CziReaderTestAccess::MakeWithSubblocks(MakeMultiPlaneSubblocks());
  const auto scene = MakeMultiPlaneScene(*reader);

  EXPECT_FALSE(scene->PrepareRequest(FullLevelRequest(/*level=*/0, 2, 0)).ok());
  EXPECT_FALSE(scene->PrepareRequest(FullLevelRequest(/*level=*/0, 0, 2)).ok());
}

TEST(CziScenePlaneTest, SinglePlaneSceneDefaultsToOneByOneStack) {
  auto reader = CziReaderTestAccess::MakeWithSubblocks(
      std::vector<CziSubblockInfo>{MakeTile(0, /*z=*/0, /*t=*/0)});
  const auto scene = std::make_unique<CziSceneImage>(
      *reader, /*scene_id=*/0, "Scene 0",
      /*subblock_indices=*/std::vector<uint32_t>{0}, /*mpp_x=*/0.0,
      /*mpp_y=*/0.0, /*objective_magnification=*/0.0, "Zeiss");

  const StackInfo info = scene->GetStackInfo();
  EXPECT_EQ(info.z_count, 1u);
  EXPECT_EQ(info.t_count, 1u);
  EXPECT_FALSE(info.z_spacing_um.has_value());
  EXPECT_FALSE(info.t_interval_s.has_value());

  // The default plane still plans normally.
  auto plan_or = scene->PrepareRequest(FullLevelRequest(/*level=*/0, 0, 0));
  ASSERT_TRUE(plan_or.ok()) << plan_or.status().ToString();
  EXPECT_EQ(plan_or->operations.size(), 1u);
}

}  // namespace
}  // namespace fastslide
