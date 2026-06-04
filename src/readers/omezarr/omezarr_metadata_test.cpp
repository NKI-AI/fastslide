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

#include "fastslide/readers/omezarr/omezarr_metadata.h"

#include <gtest/gtest.h>

#include <string>

namespace fastslide::formats::omezarr {
namespace {

constexpr std::string_view kRootJson = R"({
  "attributes": {
    "ome": {
      "version": "0.5",
      "multiscales": [
        {
          "datasets": [
            {"path": "s0", "coordinateTransformations": [
              {"type": "scale", "scale": [1.0, 1.0, 1.0]}]},
            {"path": "s1", "coordinateTransformations": [
              {"type": "scale", "scale": [1.0, 2.0, 2.0]}]}
          ],
          "axes": [
            {"name": "c", "type": "channel"},
            {"name": "y", "type": "space"},
            {"name": "x", "type": "space"}
          ]
        }
      ]
    }
  },
  "zarr_format": 3,
  "node_type": "group"
})";

constexpr std::string_view kArrayJson = R"({
  "shape": [5, 34560, 24960],
  "data_type": "uint8",
  "chunk_grid": {
    "name": "regular",
    "configuration": {"chunk_shape": [5, 5181, 5181]}
  },
  "chunk_key_encoding": {
    "name": "default",
    "configuration": {"separator": "/"}
  },
  "fill_value": 0,
  "codecs": [
    {"name": "bytes"},
    {"name": "zstd", "configuration": {"level": 0, "checksum": false}}
  ],
  "dimension_names": ["c", "y", "x"],
  "zarr_format": 3,
  "node_type": "array"
})";

TEST(OmezarrMetadataTest, ParsesRootMultiscalesAndAxes) {
  auto result = OmeZarrMetadataParser::ParseRootJson(kRootJson);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const OmeNgffMetadata& meta = result.value();
  EXPECT_EQ(meta.version, "0.5");
  ASSERT_EQ(meta.axes.size(), 3u);
  EXPECT_EQ(meta.axes[0].name, "c");
  EXPECT_EQ(meta.axes[1].name, "y");
  EXPECT_EQ(meta.axes[2].name, "x");
  ASSERT_EQ(meta.datasets.size(), 2u);
  EXPECT_EQ(meta.datasets[0].path, "s0");
  EXPECT_EQ(meta.datasets[1].path, "s1");
  ASSERT_EQ(meta.datasets[1].scale.size(), 3u);
  EXPECT_DOUBLE_EQ(meta.datasets[1].scale[1], 2.0);

  ASSERT_TRUE(meta.ChannelAxis().has_value());
  EXPECT_EQ(*meta.ChannelAxis(), 0u);
  ASSERT_TRUE(meta.YAxis().has_value());
  EXPECT_EQ(*meta.YAxis(), 1u);
  ASSERT_TRUE(meta.XAxis().has_value());
  EXPECT_EQ(*meta.XAxis(), 2u);
}

TEST(OmezarrMetadataTest, ParsesArrayShapeAndCodecs) {
  auto result = OmeZarrMetadataParser::ParseArrayJson(kArrayJson);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const ZarrArrayMetadata& meta = result.value();
  ASSERT_EQ(meta.shape.size(), 3u);
  EXPECT_EQ(meta.shape[0], 5u);
  EXPECT_EQ(meta.shape[1], 34560u);
  EXPECT_EQ(meta.shape[2], 24960u);
  ASSERT_EQ(meta.chunk_shape.size(), 3u);
  EXPECT_EQ(meta.chunk_shape[1], 5181u);
  EXPECT_EQ(meta.chunk_key_separator, '/');
  EXPECT_EQ(meta.dtype.kind, ZarrDtypeKind::kUInt);
  EXPECT_EQ(meta.dtype.bits, 8u);
  ASSERT_EQ(meta.codecs.size(), 2u);
  EXPECT_EQ(meta.codecs[0].name, "bytes");
  EXPECT_EQ(meta.codecs[1].name, "zstd");
  ASSERT_EQ(meta.dimension_names.size(), 3u);
  EXPECT_EQ(meta.dimension_names[2], "x");
}

TEST(OmezarrMetadataTest, ParsesDtypeStrings) {
  auto u16 = OmeZarrMetadataParser::ParseDtype("uint16");
  ASSERT_TRUE(u16.ok());
  EXPECT_EQ(u16.value().kind, ZarrDtypeKind::kUInt);
  EXPECT_EQ(u16.value().bits, 16u);

  auto i32 = OmeZarrMetadataParser::ParseDtype("int32");
  ASSERT_TRUE(i32.ok());
  EXPECT_EQ(i32.value().kind, ZarrDtypeKind::kInt);
  EXPECT_EQ(i32.value().bits, 32u);

  auto f32 = OmeZarrMetadataParser::ParseDtype("float32");
  ASSERT_TRUE(f32.ok());
  EXPECT_EQ(f32.value().kind, ZarrDtypeKind::kFloat);
  EXPECT_EQ(f32.value().bits, 32u);

  auto bad = OmeZarrMetadataParser::ParseDtype("complex64");
  EXPECT_FALSE(bad.ok());
}

TEST(OmezarrMetadataTest, RejectsRootWithoutSpatialAxes) {
  constexpr std::string_view kBadAxes = R"({
    "attributes": {"ome": {"version": "0.5", "multiscales": [{
      "datasets": [{"path": "s0"}],
      "axes": [{"name": "c", "type": "channel"}]
    }]}},
    "zarr_format": 3, "node_type": "group"})";
  auto result = OmeZarrMetadataParser::ParseRootJson(kBadAxes);
  EXPECT_FALSE(result.ok());
}

TEST(OmezarrMetadataTest, ParsesOmeroChannels) {
  constexpr std::string_view kOmero = R"({
    "attributes": {"ome": {
      "version": "0.5",
      "multiscales": [{
        "datasets": [{"path": "s0"}],
        "axes": [
          {"name": "c", "type": "channel"},
          {"name": "y", "type": "space"},
          {"name": "x", "type": "space"}]
      }],
      "omero": {"channels": [
        {"label": "DAPI", "color": "0000FF", "active": true,
         "window": {"start": 0, "end": 65535}},
        {"label": "FITC", "color": "00FF00", "active": false}
      ]}
    }},
    "zarr_format": 3, "node_type": "group"})";
  auto result = OmeZarrMetadataParser::ParseRootJson(kOmero);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const OmeNgffMetadata& meta = result.value();
  ASSERT_EQ(meta.channels.size(), 2u);
  EXPECT_EQ(meta.channels[0].label, "DAPI");
  ASSERT_TRUE(meta.channels[0].color.has_value());
  EXPECT_EQ(meta.channels[0].color->b, 255);
  EXPECT_EQ(meta.channels[1].label, "FITC");
  EXPECT_FALSE(meta.channels[1].active);
}

TEST(OmezarrMetadataTest, ParsesOmeroColorHex) {
  auto color = OmeZarrMetadataParser::ParseOmeroColor("FF8040");
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(color->r, 255);
  EXPECT_EQ(color->g, 128);
  EXPECT_EQ(color->b, 64);

  auto with_alpha = OmeZarrMetadataParser::ParseOmeroColor("FF8040AA");
  ASSERT_TRUE(with_alpha.has_value());
  EXPECT_EQ(with_alpha->r, 255);
  EXPECT_EQ(with_alpha->g, 128);
  EXPECT_EQ(with_alpha->b, 64);

  EXPECT_FALSE(OmeZarrMetadataParser::ParseOmeroColor("xyz").has_value());
}

}  // namespace
}  // namespace fastslide::formats::omezarr
