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

#include "fastslide/readers/ometiff/ometiff_metadata_loader.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "simpletiff/index.h"

namespace fastslide {
namespace {

TEST(OmetiffMetadataLoaderTest, AggregatorRootSubifdLayoutIsMapped) {
  // Synthetic OME-XML: 3 channels, 2 pyramid levels (K=1..2).
  const std::string ome_xml =
      R"(<?xml version="1.0" encoding="UTF-8"?>
<OME xmlns="http://www.openmicroscopy.org/Schemas/OME/2016-06">
  <Image ID="Image:0">
    <Pixels DimensionOrder="XYCZT" Interleaved="false"
            PhysicalSizeX="0.5" PhysicalSizeY="0.5"
            SizeX="100" SizeY="50" SizeC="3" SizeZ="1" SizeT="1" Type="uint16">
      <Channel ID="Channel:0:0" Name="C0" Color="65280" SamplesPerPixel="1" />
      <Channel ID="Channel:0:1" Name="C1" Color="-65536" SamplesPerPixel="1" />
      <Channel ID="Channel:0:2" Name="C2" Color="-8388608" SamplesPerPixel="1" />
      <TiffData FirstC="0" FirstZ="0" FirstT="0" IFD="0" PlaneCount="1" />
      <TiffData FirstC="1" FirstZ="0" FirstT="0" IFD="1" PlaneCount="1" />
      <TiffData FirstC="2" FirstZ="0" FirstT="0" IFD="2" PlaneCount="1" />
    </Pixels>
  </Image>
  <StructuredAnnotations>
    <MapAnnotation Namespace="openmicroscopy.org/PyramidResolution">
      <Value>
        <M K="1">50 25</M>
        <M K="2">25 13</M>
      </Value>
    </MapAnnotation>
  </StructuredAnnotations>
</OME>)";

  simpletiff::TiffIndex index;

  // Root planes: 3 pages (0..2). Put OME-XML on page 0 description.
  for (int i = 0; i < 3; ++i) {
    simpletiff::PageHeader p;
    p.width = 100;
    p.height = 50;
    p.samples_per_pixel = 1;
    p.bits_per_sample = 16;
    p.new_subfile_type = 0;
    if (i == 0) {
      p.description = ome_xml;
    }
    index.AddPage(std::move(p));
  }

  // Reduced pages (6 total): level1 [3..5], level2 [6..8].
  for (int i = 0; i < 3; ++i) {
    simpletiff::PageHeader p;
    p.width = 50;
    p.height = 25;
    p.samples_per_pixel = 1;
    p.bits_per_sample = 16;
    p.new_subfile_type = 1;
    p.parent_page_index = 0u;
    index.AddPage(std::move(p));
  }
  for (int i = 0; i < 3; ++i) {
    simpletiff::PageHeader p;
    p.width = 25;
    p.height = 13;
    p.samples_per_pixel = 1;
    p.bits_per_sample = 16;
    p.new_subfile_type = 1;
    p.parent_page_index = 0u;
    index.AddPage(std::move(p));
  }

  // Encode children of root 0 as [level1 pages..., level2 pages...].
  const std::vector<uint32_t> children = {3, 4, 5, 6, 7, 8};
  index.MutablePage(0).sub_pages = index.AppendChildPages(children);
  // Roots 1 and 2 have no sub pages.
  index.MutablePage(1).sub_pages = index.AppendChildPages({});
  index.MutablePage(2).sub_pages = index.AppendChildPages({});

  OmeSlideMetadata md;
  std::vector<OmeTiffChannelInfo> channels;
  std::vector<OmeTiffLevelInfo> pyramid;
  std::map<std::string, uint32_t> assoc;

  ASSERT_TRUE(
      OmetiffMetadataLoader::LoadMetadata(index, md, channels, pyramid, assoc)
          .ok());
  EXPECT_EQ(channels.size(), 3u);
  // Channel colors come from OME Channel@Color encoded by Bio-Formats.
  // Expect RGBA decoding: 65280 == 0x0000FF00 => blue.
  EXPECT_EQ(channels[0].color, ColorRGB(0, 0, 255));
  // -65536 == 0xFFFF0000 => yellow.
  EXPECT_EQ(channels[1].color, ColorRGB(255, 255, 0));
  // -8388608 == 0xFF800000 => orange (255,128,0).
  EXPECT_EQ(channels[2].color, ColorRGB(255, 128, 0));
  ASSERT_EQ(pyramid.size(), 3u);  // level0 + 2 reduced
  EXPECT_EQ(pyramid[0].pages.size(), 3u);
  EXPECT_EQ(pyramid[1].pages.size(), 3u);
  EXPECT_EQ(pyramid[2].pages.size(), 3u);
  EXPECT_EQ(pyramid[1].pages[0], 3u);
  EXPECT_EQ(pyramid[1].pages[2], 5u);
  EXPECT_EQ(pyramid[2].pages[0], 6u);
  EXPECT_EQ(pyramid[2].pages[2], 8u);
}

TEST(OmetiffMetadataLoaderTest, InterleavedRgbSinglePlaneIsMapped) {
  // Interleaved RGB OME: SizeC=3 but stored as a single IFD with
  // SamplesPerPixel=3.
  const std::string ome_xml =
      R"(<?xml version="1.0" encoding="UTF-8"?>
<OME xmlns="http://www.openmicroscopy.org/Schemas/OME/2016-06">
  <Image ID="Image:0">
    <Pixels DimensionOrder="XYCZT" Interleaved="true"
            PhysicalSizeX="0.24" PhysicalSizeY="0.24"
            SizeX="100" SizeY="50" SizeC="3" SizeZ="1" SizeT="1" Type="uint8">
      <Channel ID="Channel:0" SamplesPerPixel="3" />
      <TiffData FirstC="0" FirstZ="0" FirstT="0" IFD="0" PlaneCount="1" />
    </Pixels>
  </Image>
  <StructuredAnnotations>
    <MapAnnotation Namespace="openmicroscopy.org/PyramidResolution">
      <Value>
        <M K="1">50 25</M>
        <M K="2">25 13</M>
      </Value>
    </MapAnnotation>
  </StructuredAnnotations>
</OME>)";

  simpletiff::TiffIndex index;

  // Root RGB plane.
  {
    simpletiff::PageHeader p;
    p.width = 100;
    p.height = 50;
    p.samples_per_pixel = 3;
    p.bits_per_sample = 8;
    p.new_subfile_type = 0;
    p.description = ome_xml;
    index.AddPage(std::move(p));
  }

  // Two reduced RGB levels as SubIFDs.
  {
    simpletiff::PageHeader p;
    p.width = 50;
    p.height = 25;
    p.samples_per_pixel = 3;
    p.bits_per_sample = 8;
    p.new_subfile_type = 1;
    p.parent_page_index = 0u;
    index.AddPage(std::move(p));
  }
  {
    simpletiff::PageHeader p;
    p.width = 25;
    p.height = 13;
    p.samples_per_pixel = 3;
    p.bits_per_sample = 8;
    p.new_subfile_type = 1;
    p.parent_page_index = 0u;
    index.AddPage(std::move(p));
  }

  const std::array<uint32_t, 2> children = {1u, 2u};
  index.MutablePage(0).sub_pages = index.AppendChildPages(children);

  OmeSlideMetadata md;
  std::vector<OmeTiffChannelInfo> channels;
  std::vector<OmeTiffLevelInfo> pyramid;
  std::map<std::string, uint32_t> assoc;

  ASSERT_TRUE(
      OmetiffMetadataLoader::LoadMetadata(index, md, channels, pyramid, assoc)
          .ok());
  EXPECT_EQ(channels.size(), 1u);
  ASSERT_EQ(pyramid.size(), 3u);
  EXPECT_EQ(pyramid[0].pages.size(), 1u);
  EXPECT_EQ(pyramid[1].pages.size(), 1u);
  EXPECT_EQ(pyramid[2].pages.size(), 1u);
  EXPECT_EQ(pyramid[0].pages[0], 0u);
  EXPECT_EQ(pyramid[1].pages[0], 1u);
  EXPECT_EQ(pyramid[2].pages[0], 2u);
}

}  // namespace
}  // namespace fastslide
