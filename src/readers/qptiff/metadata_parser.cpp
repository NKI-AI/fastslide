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

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <pugixml.hpp>

#include "absl/status/status.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/utilities/colors.h"

namespace fastslide {
namespace formats {
namespace qptiff {

absl::Status QpTiffMetadataParser::ParseSlideMetadata(
    const std::string& xml_content, QpTiffSlideMetadata& metadata) {

  if (!IsQpTiffFormat(xml_content)) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Invalid QPTIFF XML format");
  }

  pugi::xml_document doc;
  if (!doc.load_string(xml_content.c_str())) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Failed to parse XML metadata");
  }

  auto root = doc.child("PerkinElmer-QPI-ImageDescription");
  if (root.empty()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Invalid XML structure - missing root element");
  }

  return ExtractResolutionInfo(root, metadata);
}

absl::StatusOr<QpTiffChannelInfo> QpTiffMetadataParser::ParseChannelInfo(
    const std::string& xml_content, int channel_index) {

  pugi::xml_document doc;
  if (!doc.load_string(xml_content.c_str())) {
    return absl::InvalidArgumentError("Failed to parse XML metadata");
  }

  auto root = doc.child("PerkinElmer-QPI-ImageDescription");
  if (root.empty()) {
    return absl::InvalidArgumentError("Invalid XML structure");
  }

  QpTiffChannelInfo channel;

  // Extract basic channel information
  std::string name = GetText(&root, "Name");
  channel.name =
      name.empty() ? "Channel " + std::to_string(channel_index + 1) : name;

  // Extract biomarker information
  std::string biomarker = GetText(&root, "Biomarker");
  channel.biomarker = biomarker.empty() ? "Unknown Biomarker " +
                                              std::to_string(channel_index + 1)
                                        : biomarker;

  // Extract exposure time
  std::string exposure_str = GetText(&root, "ExposureTime");
  channel.exposure_time = exposure_str.empty() ? 0 : std::stoull(exposure_str);

  // Extract signal units
  std::string signal_units_str = GetText(&root, "SignalUnits");
  channel.signal_units =
      signal_units_str.empty() ? 0 : std::stoull(signal_units_str);

  // Extract and parse color
  std::string color_str = GetText(&root, "Color");
  ColorRGB default_color = GetDefaultChannelColor(channel_index + 1);
  channel.color = ParseChannelColor(color_str, default_color);

  return channel;
}

std::string QpTiffMetadataParser::ExtractImageType(
    const std::string& xml_content) {
  pugi::xml_document doc;
  if (!doc.load_string(xml_content.c_str())) {
    return "";
  }

  auto root = doc.child("PerkinElmer-QPI-ImageDescription");
  if (root.empty()) {
    return "";
  }

  return GetText(&root, "ImageType");
}

bool QpTiffMetadataParser::IsQpTiffFormat(const std::string& xml_content) {
  return xml_content.find("PerkinElmer-QPI-ImageDescription") !=
         std::string::npos;
}

std::string QpTiffMetadataParser::GetText(const void* node_ptr,
                                          const char* tag) {
  const auto* const node = static_cast<const pugi::xml_node*>(node_ptr);
  auto child = node->child(tag);
  return !child.empty() ? child.text().as_string() : std::string{};
}

absl::Status QpTiffMetadataParser::ExtractResolutionInfo(
    const pugi::xml_node& root_node, QpTiffSlideMetadata& metadata) {

  auto resolution_node = root_node.child("ScanProfile").child("root");
  if (resolution_node.empty()) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       "Resolution information not found in XML");
  }

  // Extract pixel size
  auto pixel_size_node = resolution_node.child("PixelSizeMicrons");
  if (!pixel_size_node.empty()) {
    double pixel_size = pixel_size_node.text().as_double();
    if (pixel_size > 0.0) {
      metadata.mpp_x = pixel_size;
      metadata.mpp_y = pixel_size;
    }
  }

  // Extract magnification
  auto magnification_node = resolution_node.child("Magnification");
  if (!magnification_node.empty()) {
    metadata.magnification = magnification_node.text().as_double();
  }

  // Extract objective name
  auto objective_node = resolution_node.child("ObjectiveName");
  if (!objective_node.empty()) {
    metadata.objective_name = objective_node.text().as_string();
  }

  if (metadata.mpp_x <= 0.0 || metadata.mpp_y <= 0.0) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       "Valid pixel size not found in XML");
  }

  return absl::OkStatus();
}

ColorRGB QpTiffMetadataParser::ParseChannelColor(
    const std::string& color_str, const ColorRGB& default_color) {

  if (color_str.empty()) {
    return default_color;
  }

  auto color_result = ParseRgb(color_str);
  if (!color_result.ok()) {
    return default_color;
  }

  const auto& rgb_array = color_result.value();
  return ColorRGB(rgb_array[0], rgb_array[1], rgb_array[2]);
}

}  // namespace qptiff
}  // namespace formats
}  // namespace fastslide
