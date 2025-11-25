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

#include "fastslide/readers/aperio/metadata_parser.h"

#include "gtest/gtest.h"

namespace fastslide {
namespace formats {
namespace aperio {
namespace {

class AperioMetadataParserTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {}
};

TEST_F(AperioMetadataParserTest, ParseValidAperioDescription) {
  std::string description =
      "Aperio Image Library v10.0.51|"
      "46920x33014 [0,100 46000x32914] (256x256) "
      "JPEG/RGB Q=30|AppMag = 20|StripeWidth = 2040|"
      "ScanScope ID = CPAPERIOCS|MPP = 0.4990|Left = 25.681381|"
      "Top = 23.449873|LineCameraSkew = -0.000424|"
      "LineAreaXOffset = 0.019265|LineAreaYOffset = -0.000313|"
      "Focus Offset = 0.000000|DSR ID = AP_26_500";

  AperioMetadata metadata;
  auto status =
      AperioMetadataParser::ParseFromDescription(description, metadata);

  EXPECT_TRUE(status.ok());
  EXPECT_NEAR(metadata.mpp[0], 0.4990, 1e-6);
  EXPECT_NEAR(metadata.mpp[1], 0.4990, 1e-6);
  EXPECT_NEAR(metadata.app_mag, 20.0, 1e-6);
  EXPECT_EQ(metadata.scanner_id, "CPAPERIOCS");
}

TEST_F(AperioMetadataParserTest, ParseInvalidDescription) {
  std::string description = "Not a valid file format at all";

  AperioMetadata metadata;
  auto status =
      AperioMetadataParser::ParseFromDescription(description, metadata);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(AperioMetadataParserTest, ParseAperioDescriptionWithoutMetadata) {
  std::string description = "Aperio Image Library but no valid metadata fields";

  AperioMetadata metadata;
  auto status =
      AperioMetadataParser::ParseFromDescription(description, metadata);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(AperioMetadataParserTest, IsAperioFormatDetection) {
  EXPECT_TRUE(
      AperioMetadataParser::IsAperioFormat("Aperio Image Library v10.0.51"));
  EXPECT_TRUE(
      AperioMetadataParser::IsAperioFormat("Some Aperio metadata here"));
  EXPECT_FALSE(AperioMetadataParser::IsAperioFormat("Regular TIFF file"));
  EXPECT_FALSE(AperioMetadataParser::IsAperioFormat(""));
}

TEST_F(AperioMetadataParserTest, ParseAssociatedImageName) {
  EXPECT_EQ(
      AperioMetadataParser::ParseAssociatedImageName("Image contains macro"),
      "macro");
  EXPECT_EQ(
      AperioMetadataParser::ParseAssociatedImageName("This is a label image"),
      "label");
  EXPECT_EQ(AperioMetadataParser::ParseAssociatedImageName("thumbnail preview"),
            "thumbnail");
  EXPECT_EQ(
      AperioMetadataParser::ParseAssociatedImageName("unknown image type"), "");
}

TEST_F(AperioMetadataParserTest, ParsePartialMetadata) {
  // Test with only some metadata fields present
  std::string description = "Aperio Image Library|MPP = 0.25|Some other data";

  AperioMetadata metadata;
  auto status =
      AperioMetadataParser::ParseFromDescription(description, metadata);

  EXPECT_TRUE(status.ok());
  EXPECT_NEAR(metadata.mpp[0], 0.25, 1e-6);
  EXPECT_NEAR(metadata.mpp[1], 0.25, 1e-6);
  EXPECT_EQ(metadata.app_mag, 0.0);    // Not set
  EXPECT_EQ(metadata.scanner_id, "");  // Not set
}

TEST_F(AperioMetadataParserTest, ParseWithWhitespace) {
  // Test parsing with extra whitespace
  std::string description =
      "Aperio Image Library| MPP =  0.5  |  AppMag = 40  ";

  AperioMetadata metadata;
  auto status =
      AperioMetadataParser::ParseFromDescription(description, metadata);

  EXPECT_TRUE(status.ok());
  EXPECT_NEAR(metadata.mpp[0], 0.5, 1e-6);
  EXPECT_NEAR(metadata.app_mag, 40.0, 1e-6);
}

}  // namespace
}  // namespace aperio
}  // namespace formats
}  // namespace fastslide
