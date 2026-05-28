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

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "fastslide/core/tile_request.h"
#include "fastslide/readers/bif/bif.h"
#include "fastslide/readers/bif/bif_stitcher.h"
#include "fastslide/readers/bif/bif_xml.h"
#include "gtest/gtest.h"

namespace fastslide {
namespace bif {
namespace {

TEST(BifXmlTest, LooksLikeBifDetectsIScanMarker) {
  EXPECT_TRUE(LooksLikeBif(R"(<iScan ScannerModel="VENTANA DP 200"/>)"));
  EXPECT_FALSE(LooksLikeBif("<svs><image/></svs>"));
  EXPECT_FALSE(LooksLikeBif(""));
}

TEST(BifXmlTest, ParseScannerInfoReadsIScanAttributes) {
  const std::string xmp = R"(<iScan ScannerModel="VENTANA DP 200"
        Magnification="40" ScanRes="0.2522" ScanWhitePoint="240"
        Barcode1D="ABC123" Z-layers="1"/>)";
  auto info_or = ParseScannerInfo(xmp);
  ASSERT_TRUE(info_or.ok());
  const ScannerInfo& info = *info_or;
  EXPECT_EQ(info.scanner_model, "VENTANA DP 200");
  EXPECT_DOUBLE_EQ(info.magnification, 40.0);
  EXPECT_DOUBLE_EQ(info.scan_res, 0.2522);
  EXPECT_EQ(info.scan_white_point, 240);
  EXPECT_EQ(info.barcode_1d, "ABC123");
}

TEST(BifXmlTest, ParseScannerInfoDefaultsWhitePointTo255) {
  auto info_or = ParseScannerInfo(R"(<iScan ScannerModel="X"/>)");
  ASSERT_TRUE(info_or.ok());
  EXPECT_EQ(info_or->scan_white_point, 255);
}

TEST(BifXmlTest, ParseEncodeInfoReadsAoiAndJoints) {
  const std::string xmp = R"(<EncodeInfo>
      <SlideStitchInfo>
        <ImageInfo AOIIndex="0" NumRows="2" NumCols="2"
                   Width="512" Height="512" Pos-X="100" Pos-Y="200">
          <TileJointInfo Direction="LEFT" Tile1="2" Tile2="1"
                         OverlapX="10" OverlapY="0" Confidence="5"/>
          <TileJointInfo Direction="UP" Tile1="3" Tile2="1"
                         OverlapX="0" OverlapY="7" Confidence="9"/>
        </ImageInfo>
      </SlideStitchInfo>
      <AoiOrigin>
        <AOI0 OriginX="0" OriginY="0"/>
      </AoiOrigin>
    </EncodeInfo>)";

  auto encode_or = ParseEncodeInfo(xmp);
  ASSERT_TRUE(encode_or.ok());
  ASSERT_EQ(encode_or->aois.size(), 1u);

  const AoiInfo& aoi = encode_or->aois.front();
  EXPECT_EQ(aoi.aoi_index, 0);
  EXPECT_EQ(aoi.num_rows, 2);
  EXPECT_EQ(aoi.num_cols, 2);
  EXPECT_DOUBLE_EQ(aoi.tile_width, 512.0);
  EXPECT_DOUBLE_EQ(aoi.pos_x, 100.0);
  EXPECT_DOUBLE_EQ(aoi.pos_y, 200.0);
  ASSERT_EQ(aoi.joints.size(), 2u);
  EXPECT_EQ(aoi.joints[0].direction, JointDirection::kLeft);
  EXPECT_DOUBLE_EQ(aoi.joints[0].overlap_x, 10.0);
  EXPECT_EQ(aoi.joints[0].confidence, 5);
  EXPECT_EQ(aoi.joints[1].direction, JointDirection::kUp);
  EXPECT_DOUBLE_EQ(aoi.joints[1].overlap_y, 7.0);
}

TEST(BifXmlTest, ParseEncodeInfoFailsWithoutImageInfo) {
  EXPECT_FALSE(ParseEncodeInfo("<EncodeInfo></EncodeInfo>").ok());
}

TEST(BifXmlTest, ParseEncodeInfoSkipsUnscannedAois) {
  // The first AOI failed to scan (AOIScanned="0") and must be dropped; only
  // the scanned AOI (index 1) survives.
  const std::string xmp = R"(<EncodeInfo>
      <SlideStitchInfo>
        <ImageInfo AOIScanned="0" AOIIndex="0" NumRows="2" NumCols="2"
                   Width="512" Height="512"/>
        <ImageInfo AOIScanned="1" AOIIndex="1" NumRows="2" NumCols="2"
                   Width="512" Height="512"/>
      </SlideStitchInfo>
      <AoiOrigin>
        <AOI1 OriginX="0" OriginY="0"/>
      </AoiOrigin>
    </EncodeInfo>)";

  auto encode_or = ParseEncodeInfo(xmp);
  ASSERT_TRUE(encode_or.ok());
  ASSERT_EQ(encode_or->aois.size(), 1u);
  EXPECT_EQ(encode_or->aois.front().aoi_index, 1);
}

TEST(BifXmlTest, ParseEncodeInfoKeepsAoiWhenScannedAttributeAbsent) {
  // A missing AOIScanned attribute defaults to scanned (1), so the AOI is kept.
  const std::string xmp = R"(<EncodeInfo>
      <SlideStitchInfo>
        <ImageInfo AOIIndex="0" NumRows="1" NumCols="1"
                   Width="512" Height="512"/>
      </SlideStitchInfo>
      <AoiOrigin>
        <AOI0 OriginX="0" OriginY="0"/>
      </AoiOrigin>
    </EncodeInfo>)";

  auto encode_or = ParseEncodeInfo(xmp);
  ASSERT_TRUE(encode_or.ok());
  EXPECT_EQ(encode_or->aois.size(), 1u);
}

TEST(BifStitcherTest, SerpentineColumnSnakesByRow) {
  // 3 columns. Even rows go left->right, odd rows right->left.
  EXPECT_EQ(SerpentineColumn(1, 3), 0);
  EXPECT_EQ(SerpentineColumn(2, 3), 1);
  EXPECT_EQ(SerpentineColumn(3, 3), 2);
  EXPECT_EQ(SerpentineColumn(4, 3), 2);  // row 1, reversed
  EXPECT_EQ(SerpentineColumn(5, 3), 1);
  EXPECT_EQ(SerpentineColumn(6, 3), 0);
}

TEST(BifStitcherTest, PlacesGridWithDefaultAdvanceWhenNoJoints) {
  EncodeInfo encode;
  AoiInfo aoi;
  aoi.aoi_index = 0;
  aoi.num_rows = 1;
  aoi.num_cols = 2;
  aoi.tile_width = 512;
  aoi.tile_height = 512;
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, /*grid_cols=*/2);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  ASSERT_EQ(s.tiles.size(), 2u);
  EXPECT_DOUBLE_EQ(s.tiles[0].x, 0.0);
  EXPECT_DOUBLE_EQ(s.tiles[1].x, 512.0);
  EXPECT_EQ(s.level0_width, 1024u);
  EXPECT_EQ(s.level0_height, 512u);
}

TEST(BifStitcherTest, HorizontalOverlapReducesColumnPitch) {
  // 3 columns, one row. A horizontal joint (LEFT/RIGHT) reports OverlapX=10, so
  // the horizontal grid pitch is tile_w - 10 = 502 and every column advances by
  // that pitch. The overlaps describe a uniform serpentine grid, so the mean
  // overlap sets one pitch for the whole AOI.
  EncodeInfo encode;
  AoiInfo aoi;
  aoi.num_rows = 1;
  aoi.num_cols = 3;
  aoi.tile_width = 512;
  aoi.tile_height = 512;

  TileJoint left;
  left.direction = JointDirection::kLeft;
  left.tile1 = 2;
  left.tile2 = 1;
  left.overlap_x = 10;
  left.confidence = 100;
  left.flag_joined = true;
  aoi.joints.push_back(left);
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, /*grid_cols=*/3);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  ASSERT_EQ(s.tiles.size(), 3u);
  EXPECT_DOUBLE_EQ(s.tiles[0].x, 0.0);
  EXPECT_DOUBLE_EQ(s.tiles[1].x, 502.0);
  EXPECT_DOUBLE_EQ(s.tiles[2].x, 1004.0);
  EXPECT_EQ(s.level0_width, static_cast<uint32_t>(1004 + 512));
}

TEST(BifStitcherTest, LeftOverlapsAppliedPerBoundaryNotAveraged) {
  // 3 columns, one row, with two LEFT joints carrying DIFFERENT overlaps: the
  // col0|col1 boundary abuts (OverlapX=0) while the col1|col2 boundary overlaps
  // by 24. A correct per-boundary layout keeps col1 at the full pitch (512) and
  // shifts only col2 left by 24 (to 1000). A uniform average would instead
  // spread the mean (12) across both boundaries, wrongly pulling col1 to 500 -
  // this is the LEFT regression this test guards against.
  EncodeInfo encode;
  AoiInfo aoi;
  aoi.num_rows = 1;
  aoi.num_cols = 3;
  aoi.tile_width = 512;
  aoi.tile_height = 512;

  TileJoint abut;
  abut.direction = JointDirection::kLeft;
  abut.tile1 = 1;  // col0
  abut.tile2 = 2;  // col1
  abut.overlap_x = 0;
  abut.confidence = 100;
  abut.flag_joined = true;
  aoi.joints.push_back(abut);

  TileJoint seam;
  seam.direction = JointDirection::kLeft;
  seam.tile1 = 2;  // col1
  seam.tile2 = 3;  // col2
  seam.overlap_x = 24;
  seam.confidence = 100;
  seam.flag_joined = true;
  aoi.joints.push_back(seam);
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, /*grid_cols=*/3);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  ASSERT_EQ(s.tiles.size(), 3u);
  EXPECT_DOUBLE_EQ(s.tiles[0].x, 0.0);
  EXPECT_DOUBLE_EQ(s.tiles[1].x, 512.0);
  EXPECT_DOUBLE_EQ(s.tiles[2].x, 1000.0);
}

TEST(BifStitcherTest, RightJointReducesHorizontalAdvance) {
  EncodeInfo encode;
  AoiInfo aoi;
  aoi.num_rows = 1;
  aoi.num_cols = 2;
  aoi.tile_width = 512;
  aoi.tile_height = 512;

  TileJoint right;
  right.direction = JointDirection::kRight;
  right.tile1 = 1;
  right.tile2 = 2;
  right.overlap_x = 20;
  right.confidence = 100;
  right.flag_joined = true;
  aoi.joints.push_back(right);
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, /*grid_cols=*/2);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  ASSERT_EQ(s.tiles.size(), 2u);
  // adv_x = tile_w - overlap_x = 492.
  EXPECT_DOUBLE_EQ(s.tiles[1].x - s.tiles[0].x, 492.0);
}

TEST(BifStitcherTest, DownDirectionIsRejectedAsUnverified) {
  // DOWN has whitepaper-defined geometry (identical to UP) but is never emitted
  // by any sample DP 200 file, so the stitcher refuses to place it and returns
  // an error rather than silently producing an unchecked layout.
  EncodeInfo encode;
  AoiInfo aoi;
  aoi.num_rows = 2;
  aoi.num_cols = 1;
  aoi.tile_width = 512;
  aoi.tile_height = 512;

  TileJoint down;
  down.direction = JointDirection::kDown;
  down.tile1 = 2;
  down.tile2 = 1;
  down.overlap_x = 0;
  down.overlap_y = 30;
  down.confidence = 100;
  down.flag_joined = true;
  aoi.joints.push_back(down);
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, /*grid_cols=*/1);
  ASSERT_FALSE(stitch_or.ok());
  EXPECT_EQ(stitch_or.status().code(), aifocore::StatusCode::kUnimplemented);
}

TEST(BifStitcherTest, VerticalOverlapReducesRowPitch) {
  // 1 column, 2 rows. A vertical joint (UP/DOWN) reports OverlapY=30, so the
  // vertical grid pitch is tile_h - 30 = 482. Vertical overlaps feed the row
  // pitch only; horizontal placement is unaffected.
  EncodeInfo encode;
  AoiInfo aoi;
  aoi.num_rows = 2;
  aoi.num_cols = 1;
  aoi.tile_width = 512;
  aoi.tile_height = 512;

  TileJoint up;
  up.direction = JointDirection::kUp;
  up.tile1 = 1;
  up.tile2 = 2;
  up.overlap_x = 0;
  up.overlap_y = 30;
  up.confidence = 100;
  up.flag_joined = true;
  aoi.joints.push_back(up);
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, /*grid_cols=*/1);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  ASSERT_EQ(s.tiles.size(), 2u);
  EXPECT_DOUBLE_EQ(s.tiles[0].y, 0.0);
  EXPECT_DOUBLE_EQ(s.tiles[1].y, 482.0);
  EXPECT_EQ(s.level0_height, static_cast<uint32_t>(482 + 512));
}

}  // namespace
}  // namespace bif

// Guarded end-to-end smoke test. Set FASTSLIDE_BIF_TEST_FILE to the path of a
// real VENTANA BIF slide to exercise the full open + region-read pipeline.
// Skipped (not failed) when the variable is unset or the file is missing, so
// the suite stays hermetic in CI.
namespace {

TEST(BifIntegrationTest, OpensAndReadsRegion) {
  const char* path = std::getenv("FASTSLIDE_BIF_TEST_FILE");
  if (path == nullptr || !std::filesystem::exists(path)) {
    GTEST_SKIP() << "Set FASTSLIDE_BIF_TEST_FILE to a .bif slide to run this.";
  }

  auto reader_or = BifReader::Create(path);
  ASSERT_TRUE(reader_or.ok()) << reader_or.status().message();
  auto& reader = *reader_or;

  ASSERT_GT(reader->GetLevelCount(), 0);
  auto level0_or = reader->GetLevelInfo(0);
  ASSERT_TRUE(level0_or.ok());
  const auto dims = level0_or->dimensions;
  EXPECT_GT(dims[0], 0u);
  EXPECT_GT(dims[1], 0u);

  // Read a small region from the top-left of level 0.
  const uint32_t w = std::min<uint32_t>(256, dims[0]);
  const uint32_t h = std::min<uint32_t>(256, dims[1]);
  RegionSpec region;
  region.top_left = ImageCoordinate{0, 0};
  region.size = ImageDimensions{w, h};
  region.level = 0;
  auto image_or = reader->ReadRegion(region);
  ASSERT_TRUE(image_or.ok()) << image_or.status().message();
  EXPECT_EQ(image_or->GetDimensions()[0], w);
  EXPECT_EQ(image_or->GetDimensions()[1], h);

  // Read the lowest-resolution level in full to exercise pyramid re-stitch.
  const int last = reader->GetLevelCount() - 1;
  auto last_or = reader->GetLevelInfo(last);
  ASSERT_TRUE(last_or.ok());
  RegionSpec full;
  full.top_left = ImageCoordinate{0, 0};
  full.size = ImageDimensions{last_or->dimensions[0], last_or->dimensions[1]};
  full.level = last;
  EXPECT_TRUE(reader->ReadRegion(full).ok());
}

}  // namespace
}  // namespace fastslide
