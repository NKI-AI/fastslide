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

#include <cmath>
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

TEST(BifXmlTest, ParseEncodeInfoPairsOriginsByOrderWithoutAoiIndex) {
  // Without AOIIndex the n-th <ImageInfo> pairs with <AOIn>, counting the
  // unscanned AOI in between (the layout of OpenSlide's OS-2.bif).
  const std::string xmp = R"(<EncodeInfo>
      <SlideStitchInfo>
        <ImageInfo AOIScanned="1" NumRows="17" NumCols="11"
                   Width="1024" Height="1360"/>
        <ImageInfo AOIScanned="0" NumRows="10" NumCols="12"
                   Width="1024" Height="1360"/>
        <ImageInfo AOIScanned="1" NumRows="61" NumCols="95"
                   Width="1024" Height="1360"/>
      </SlideStitchInfo>
      <AoiOrigin>
        <AOI0 OriginX="0" OriginY="38080"/>
        <AOI1 OriginX="6144" OriginY="68000"/>
        <AOI2 OriginX="30720" OriginY="0"/>
      </AoiOrigin>
    </EncodeInfo>)";

  auto encode_or = ParseEncodeInfo(xmp);
  ASSERT_TRUE(encode_or.ok());
  ASSERT_EQ(encode_or->aois.size(), 2u);
  EXPECT_EQ(encode_or->aois[0].aoi_index, 0);
  EXPECT_DOUBLE_EQ(encode_or->aois[0].origin_x, 0.0);
  EXPECT_DOUBLE_EQ(encode_or->aois[0].origin_y, 38080.0);
  EXPECT_EQ(encode_or->aois[1].aoi_index, 2);
  EXPECT_DOUBLE_EQ(encode_or->aois[1].origin_x, 30720.0);
  EXPECT_DOUBLE_EQ(encode_or->aois[1].origin_y, 0.0);
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

// One AOI of `rows` x `cols` tiles at stage position (0, 0), AoiOrigin (0, 0).
AoiInfo MakeAoi(int rows, int cols) {
  AoiInfo aoi;
  aoi.num_rows = rows;
  aoi.num_cols = cols;
  return aoi;
}

TileJoint MakeJoint(JointDirection direction, int tile1, int tile2,
                    double overlap, int confidence = 100) {
  TileJoint joint;
  joint.direction = direction;
  joint.tile1 = tile1;
  joint.tile2 = tile2;
  if (direction == JointDirection::kUp || direction == JointDirection::kDown) {
    joint.overlap_y = overlap;
  } else {
    joint.overlap_x = overlap;
  }
  joint.confidence = confidence;
  return joint;
}

TEST(BifStitcherTest, OverlapModelForScanner) {
  EXPECT_EQ(OverlapModelFor("VENTANA DP 200"), OverlapModel::kPerTile);
  EXPECT_EQ(OverlapModelFor("VENTANA iScan HT"), OverlapModel::kMeanAdvance);
  EXPECT_EQ(OverlapModelFor(""), OverlapModel::kMeanAdvance);
}

TEST(BifStitcherTest, RoundUpToMultipleOf16) {
  EXPECT_EQ(RoundUpToMultipleOf16(1024.0), 1024u);
  EXPECT_EQ(RoundUpToMultipleOf16(1014.0), 1024u);
  EXPECT_EQ(RoundUpToMultipleOf16(1024.1), 1040u);
  EXPECT_EQ(RoundUpToMultipleOf16(0.5), 16u);
}

TEST(BifStitcherTest, TilesAbutWithoutJoints) {
  EncodeInfo encode;
  encode.aois.push_back(MakeAoi(1, 2));
  for (const OverlapModel model :
       {OverlapModel::kMeanAdvance, OverlapModel::kPerTile}) {
    auto stitch_or = StitchLevel0(encode, 512, 512, model);
    ASSERT_TRUE(stitch_or.ok());
    const StitchResult& s = *stitch_or;
    ASSERT_EQ(s.tiles.size(), 2u);
    EXPECT_DOUBLE_EQ(s.tiles[0].x, 0.0);
    EXPECT_DOUBLE_EQ(s.tiles[1].x, 512.0);
    EXPECT_EQ(s.level0_width, 1024u);
    EXPECT_EQ(s.level0_height, 512u);
  }
}

TEST(BifStitcherTest, MeanAdvanceUsesConfidenceWeightedMeanOverlap) {
  // 1 row, 3 columns; the joints overlap by 10 (confidence 100) and 30
  // (confidence 50), so every tile advances by 512 - (1000 + 1500) / 150.
  EncodeInfo encode;
  AoiInfo aoi = MakeAoi(1, 3);
  aoi.joints = {MakeJoint(JointDirection::kRight, 1, 2, 10, 100),
                MakeJoint(JointDirection::kRight, 2, 3, 30, 50)};
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, OverlapModel::kMeanAdvance);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  const double advance = 512.0 - 2500.0 / 150.0;
  ASSERT_EQ(s.tiles.size(), 3u);
  EXPECT_DOUBLE_EQ(s.tiles[0].x, 0.0);
  EXPECT_DOUBLE_EQ(s.tiles[1].x, advance);
  EXPECT_DOUBLE_EQ(s.tiles[2].x, 2 * advance);
  EXPECT_FALSE(s.align16);
  EXPECT_EQ(s.level0_width,
            static_cast<uint32_t>(std::ceil(2 * advance + 512.0)));
}

TEST(BifStitcherTest, MeanAdvanceAppliesUpJointsToRows) {
  // 2 rows, 1 column; the UP joint overlaps by 30.
  EncodeInfo encode;
  AoiInfo aoi = MakeAoi(2, 1);
  aoi.joints = {MakeJoint(JointDirection::kUp, 1, 2, 30)};
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, OverlapModel::kMeanAdvance);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  ASSERT_EQ(s.tiles.size(), 2u);
  EXPECT_DOUBLE_EQ(s.tiles[0].y, 0.0);
  EXPECT_DOUBLE_EQ(s.tiles[1].y, 482.0);
  EXPECT_EQ(s.level0_height, 994u);
}

TEST(BifStitcherTest, PlacesAoisByStagePosition) {
  // Two 1x1 AOIs. B is 700 px right of A and has the larger Pos-Y, so it sits
  // above A. A is the second tile row of the TIFF grid (AoiOrigin Y = 512)
  // and lands 88 px lower than a plain grid would put it.
  EncodeInfo encode;
  AoiInfo a = MakeAoi(1, 1);
  a.origin_y = 512;
  AoiInfo b = MakeAoi(1, 1);
  b.pos_x = 700;
  b.pos_y = 600;
  b.origin_x = 512;
  encode.aois = {a, b};

  for (const OverlapModel model :
       {OverlapModel::kMeanAdvance, OverlapModel::kPerTile}) {
    auto stitch_or = StitchLevel0(encode, 512, 512, model);
    ASSERT_TRUE(stitch_or.ok());
    const StitchResult& s = *stitch_or;
    ASSERT_EQ(s.tiles.size(), 2u);
    EXPECT_EQ(s.tiles[0].gx, 0u);
    EXPECT_EQ(s.tiles[0].gy, 1u);
    EXPECT_DOUBLE_EQ(s.tiles[0].x, 0.0);
    EXPECT_DOUBLE_EQ(s.tiles[0].y, 600.0);
    EXPECT_EQ(s.tiles[1].gx, 1u);
    EXPECT_EQ(s.tiles[1].gy, 0u);
    EXPECT_DOUBLE_EQ(s.tiles[1].x, 700.0);
    EXPECT_DOUBLE_EQ(s.tiles[1].y, 0.0);
  }
  auto mean_or = StitchLevel0(encode, 512, 512, OverlapModel::kMeanAdvance);
  EXPECT_EQ(mean_or->level0_width, 1212u);
  EXPECT_EQ(mean_or->level0_height, 1112u);
  auto per_tile_or = StitchLevel0(encode, 512, 512, OverlapModel::kPerTile);
  EXPECT_EQ(per_tile_or->level0_width, 1216u);   // 1212 rounded up to 16
  EXPECT_EQ(per_tile_or->level0_height, 1120u);  // 1112 rounded up to 16
}

TEST(BifStitcherTest, PerTileKeepsEachJointsOverlapAndAlignsTo16) {
  // 1 row, 3 columns. Only the joint of the third tile overlaps (by 10), so
  // the second tile stays at the full pitch and only the third moves left.
  EncodeInfo encode;
  AoiInfo aoi = MakeAoi(1, 3);
  aoi.joints = {MakeJoint(JointDirection::kRight, 1, 2, 0),
                MakeJoint(JointDirection::kRight, 2, 3, 10)};
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, OverlapModel::kPerTile);
  ASSERT_TRUE(stitch_or.ok());
  const StitchResult& s = *stitch_or;
  ASSERT_EQ(s.tiles.size(), 3u);
  EXPECT_DOUBLE_EQ(s.tiles[0].x, 0.0);
  EXPECT_DOUBLE_EQ(s.tiles[1].x, 512.0);
  EXPECT_DOUBLE_EQ(s.tiles[2].x, 1014.0);
  EXPECT_TRUE(s.align16);
  EXPECT_DOUBLE_EQ(s.extent_x, 1526.0);
  EXPECT_EQ(s.level0_width, 1536u);
  EXPECT_EQ(s.level0_height, 512u);
}

TEST(BifStitcherTest, PerTileOverlapsAccumulateAlongTheRow) {
  // Every joint overlaps by 10, so tile c sits at c * (512 - 10).
  EncodeInfo encode;
  AoiInfo aoi = MakeAoi(1, 3);
  aoi.joints = {MakeJoint(JointDirection::kLeft, 1, 2, 10),
                MakeJoint(JointDirection::kLeft, 2, 3, 10)};
  encode.aois.push_back(aoi);

  auto stitch_or = StitchLevel0(encode, 512, 512, OverlapModel::kPerTile);
  ASSERT_TRUE(stitch_or.ok());
  ASSERT_EQ(stitch_or->tiles.size(), 3u);
  EXPECT_DOUBLE_EQ(stitch_or->tiles[1].x, 502.0);
  EXPECT_DOUBLE_EQ(stitch_or->tiles[2].x, 1004.0);
}

TEST(BifStitcherTest, DownJointsAreRejectedAsUnverified) {
  EncodeInfo encode;
  AoiInfo aoi = MakeAoi(2, 1);
  aoi.joints = {MakeJoint(JointDirection::kDown, 1, 2, 0)};
  encode.aois.push_back(aoi);
  for (const OverlapModel model :
       {OverlapModel::kMeanAdvance, OverlapModel::kPerTile}) {
    auto stitch_or = StitchLevel0(encode, 512, 512, model);
    ASSERT_FALSE(stitch_or.ok());
    EXPECT_EQ(stitch_or.status().code(), aifocore::StatusCode::kUnimplemented);
  }
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
