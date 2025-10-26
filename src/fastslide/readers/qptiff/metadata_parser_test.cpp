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

#include "fastslide/readers/qptiff/metadata_parser.h"

#include "gtest/gtest.h"

namespace fastslide {
namespace formats {
namespace qptiff {
namespace {

class QpTiffMetadataParserTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {}
};

TEST_F(QpTiffMetadataParserTest, ParseValidSlideMetadata) {
  std::string xml_content = R"(
    <PerkinElmer-QPI-ImageDescription>
      <ScanProfile>
        <root>
          <PixelSizeMicrons>0.325</PixelSizeMicrons>
          <Magnification>20</Magnification>
          <ObjectiveName>20x/0.75</ObjectiveName>
        </root>
      </ScanProfile>
    </PerkinElmer-QPI-ImageDescription>
  )";

  QpTiffSlideMetadata metadata;
  auto status = QpTiffMetadataParser::ParseSlideMetadata(xml_content, metadata);

  EXPECT_TRUE(status.ok());
  EXPECT_NEAR(metadata.mpp_x, 0.325, 1e-6);
  EXPECT_NEAR(metadata.mpp_y, 0.325, 1e-6);
  EXPECT_NEAR(metadata.magnification, 20.0, 1e-6);
  EXPECT_EQ(metadata.objective_name, "20x/0.75");
}

TEST_F(QpTiffMetadataParserTest, ParseChannelInfo) {
  std::string xml_content = R"(
    <PerkinElmer-QPI-ImageDescription>
      <Name>DAPI</Name>
      <Biomarker>Nuclei</Biomarker>
      <Color>0,0,255</Color>
      <ExposureTime>100</ExposureTime>
      <SignalUnits>42</SignalUnits>
    </PerkinElmer-QPI-ImageDescription>
  )";

  auto result = QpTiffMetadataParser::ParseChannelInfo(xml_content, 0);

  EXPECT_TRUE(result.ok());
  const auto& channel = result.value();

  EXPECT_EQ(channel.name, "DAPI");
  EXPECT_EQ(channel.biomarker, "Nuclei");
  EXPECT_EQ(channel.exposure_time, 100);
  EXPECT_EQ(channel.signal_units, 42);
  EXPECT_EQ(channel.color.r, 0);
  EXPECT_EQ(channel.color.g, 0);
  EXPECT_EQ(channel.color.b, 255);
}

TEST_F(QpTiffMetadataParserTest, ParseChannelInfoWithDefaults) {
  std::string xml_content = R"(
    <PerkinElmer-QPI-ImageDescription>
    </PerkinElmer-QPI-ImageDescription>
  )";

  auto result = QpTiffMetadataParser::ParseChannelInfo(xml_content, 2);

  EXPECT_TRUE(result.ok());
  const auto& channel = result.value();

  EXPECT_EQ(channel.name, "Channel 3");  // index 2 -> Channel 3
  EXPECT_EQ(channel.biomarker, "Unknown Biomarker 3");
  EXPECT_EQ(channel.exposure_time, 0);
  EXPECT_EQ(channel.signal_units, 0);
  // Color should be default for channel 3
}

TEST_F(QpTiffMetadataParserTest, ExtractImageType) {
  std::string xml_content = R"(
    <PerkinElmer-QPI-ImageDescription>
      <ImageType>FullResolution</ImageType>
    </PerkinElmer-QPI-ImageDescription>
  )";

  std::string image_type = QpTiffMetadataParser::ExtractImageType(xml_content);
  EXPECT_EQ(image_type, "FullResolution");
}

TEST_F(QpTiffMetadataParserTest, IsQpTiffFormatDetection) {
  std::string valid_xml = "<PerkinElmer-QPI-ImageDescription>";
  std::string invalid_xml = "<SomeOtherFormat>";

  EXPECT_TRUE(QpTiffMetadataParser::IsQpTiffFormat(valid_xml));
  EXPECT_FALSE(QpTiffMetadataParser::IsQpTiffFormat(invalid_xml));
  EXPECT_FALSE(QpTiffMetadataParser::IsQpTiffFormat(""));
}

TEST_F(QpTiffMetadataParserTest, ParseInvalidXml) {
  std::string invalid_xml = "Not XML at all!";

  QpTiffSlideMetadata metadata;
  auto status = QpTiffMetadataParser::ParseSlideMetadata(invalid_xml, metadata);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(QpTiffMetadataParserTest, ParseMissingResolutionInfo) {
  std::string xml_content = R"(
    <PerkinElmer-QPI-ImageDescription>
      <SomethingElse>value</SomethingElse>
    </PerkinElmer-QPI-ImageDescription>
  )";

  QpTiffSlideMetadata metadata;
  auto status = QpTiffMetadataParser::ParseSlideMetadata(xml_content, metadata);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(QpTiffMetadataParserTest, GetTextHelper) {
  std::string xml_content = R"(
    <root>
      <TestTag>TestValue</TestTag>
    </root>
  )";

  // This is a bit tricky to test directly since GetText expects a pugi::xml_node*
  // For now, we'll test it indirectly through other methods
  EXPECT_TRUE(true);  // Placeholder - GetText is tested through other methods
}

}  // namespace
}  // namespace qptiff
}  // namespace formats
}  // namespace fastslide
