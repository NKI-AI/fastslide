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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_METADATA_PARSER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_METADATA_PARSER_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/utilities/colors.h"

namespace pugi {
class xml_node;
}

namespace fastslide::formats::qptiff {

/// @brief Slide-level metadata extracted from QPTIFF XML
struct QpTiffSlideMetadata {
  double mpp_x = 0.0;          ///< Microns per pixel X
  double mpp_y = 0.0;          ///< Microns per pixel Y
  double magnification = 0.0;  ///< Objective magnification
  std::string objective_name;  ///< Objective lens name
};

/// @brief Channel-specific metadata extracted from QPTIFF XML
struct QpTiffChannelInfo {
  std::string name;            ///< Channel name
  std::string biomarker;       ///< Biomarker name
  ColorRGB color;              ///< Channel display color
  uint64_t exposure_time = 0;  ///< Exposure time in milliseconds
  uint64_t signal_units = 0;   ///< Signal units/scale factor
};

/// @brief Parser for QPTIFF XML metadata
class QpTiffMetadataParser {
 public:
  /// @brief Parse slide-level metadata from QPTIFF XML
  ///
  /// Extracts resolution, magnification, and objective information
  /// from the XML metadata structure in QPTIFF files.
  ///
  /// @param xml_content Raw XML content string
  /// @param metadata Output metadata structure
  /// @return Status indicating success or failure
  static absl::Status ParseSlideMetadata(const std::string& xml_content,
                                         QpTiffSlideMetadata& metadata);

  /// @brief Parse channel information from QPTIFF XML
  ///
  /// Extracts channel-specific metadata including name, biomarker,
  /// color, and acquisition parameters.
  ///
  /// @param xml_content Raw XML content string
  /// @param channel_index Index of the channel (for default naming)
  /// @return Channel info or error status
  static absl::StatusOr<QpTiffChannelInfo> ParseChannelInfo(
      const std::string& xml_content, int channel_index);

  /// @brief Extract image type from QPTIFF XML
  ///
  /// Determines the type of image (FullResolution, ReducedResolution,
  /// Thumbnail, etc.) from the XML metadata.
  ///
  /// @param xml_content Raw XML content string
  /// @return Image type string, or empty if not found
  static std::string ExtractImageType(const std::string& xml_content);

  /// @brief Check if XML content is valid QPTIFF format
  /// @param xml_content Raw XML content string
  /// @return true if the XML appears to be QPTIFF format
  static bool IsQpTiffFormat(const std::string& xml_content);

  /// @brief Extract text content from XML node by tag name
  ///
  /// Helper function to safely extract text content from XML nodes.
  ///
  /// @param node_ptr Pointer to XML node (expected to be pugi::xml_node*)
  /// @param tag Tag name to search for
  /// @return Text content or empty string if not found
  static std::string GetText(const void* node_ptr, const char* tag);

 private:
  /// @brief Validate and extract resolution information from XML
  /// @param root_node XML root node
  /// @param metadata Output metadata structure
  /// @return Status indicating success or failure
  static absl::Status ExtractResolutionInfo(const pugi::xml_node& root_node,
                                            QpTiffSlideMetadata& metadata);

  /// @brief Extract channel color from XML color string
  /// @param color_str XML color string (format: "R,G,B")
  /// @param default_color Fallback color if parsing fails
  /// @return Parsed color or default color
  static ColorRGB ParseChannelColor(const std::string& color_str,
                                    const ColorRGB& default_color);
};

}  // namespace fastslide::formats::qptiff

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_METADATA_PARSER_H_
