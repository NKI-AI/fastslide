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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_METADATA_PARSER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_METADATA_PARSER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/utilities/colors.h"

namespace pugi {
class xml_node;
}

namespace fastslide::formats::ometiff {

/// @brief Minimal OME Pixels metadata needed to build a reader plan
struct OmePixels {
  uint32_t size_x = 0;
  uint32_t size_y = 0;
  uint32_t size_c = 0;
  uint32_t size_z = 0;
  uint32_t size_t = 0;
  std::string dimension_order;  // e.g. "XYCZT"
  bool interleaved = false;
  std::optional<double> physical_size_x;  // µm/px
  std::optional<double> physical_size_y;  // µm/px
  std::optional<double> physical_size_z;  // µm between focal planes
  std::optional<double> time_increment;   // seconds between time points
};

/// @brief OME Channel metadata
struct OmeChannel {
  uint32_t index = 0;
  std::string name;
  std::optional<int32_t> color_argb;
};

/// @brief OME TiffData mapping for planes (IFD index per plane)
struct OmeTiffData {
  uint32_t first_c = 0;
  uint32_t first_z = 0;
  uint32_t first_t = 0;
  uint32_t ifd = 0;
  uint32_t plane_count = 1;
  /// Optional `<UUID>` text of the file that physically holds these planes.
  /// Empty when the `<TiffData>` has no `<UUID>` child (the plane lives in the
  /// current file). Used to detect multi-file ("companion") datasets.
  std::string uuid;
  /// Optional `FileName` attribute of the `<UUID>` child (sibling file name).
  std::string file_name;
};

/// @brief Parsed OME-XML subset used by the OME-TIFF reader
struct OmeMetadata {
  OmePixels pixels;
  std::vector<OmeChannel> channels;
  std::vector<OmeTiffData> tiff_data;
  /// `UUID` attribute of the `<OME>` root, identifying the current file in a
  /// multi-file dataset. Empty when the writer did not emit one.
  std::string self_uuid;
  // Map K -> (size_x, size_y) for pyramid resolutions.
  std::map<int, std::pair<uint32_t, uint32_t>> pyramid_resolutions;
};

class OmeMetadataParser {
 public:
  static bool LooksLikeOmeXml(std::string_view xml);

  static aifocore::Result<OmeMetadata> Parse(std::string_view xml);

  static ColorRGB ColorFromOmeArgb(std::optional<int32_t> argb,
                                   const ColorRGB& fallback);

 private:
  static aifocore::Status ParsePixels(const pugi::xml_node& pixels_node,
                                      OmeMetadata& out);
  static aifocore::Status ParseChannels(const pugi::xml_node& pixels_node,
                                        OmeMetadata& out);
  static aifocore::Status ParseTiffData(const pugi::xml_node& pixels_node,
                                        OmeMetadata& out);
  static aifocore::Status ParsePyramidResolutions(
      const pugi::xml_node& ome_root, OmeMetadata& out);
};

}  // namespace fastslide::formats::ometiff

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_METADATA_PARSER_H_
