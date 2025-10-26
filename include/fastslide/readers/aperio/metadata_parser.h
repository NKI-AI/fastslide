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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_METADATA_PARSER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_METADATA_PARSER_H_

#include <string>

#include "absl/status/status.h"
#include "aifocore/concepts/numeric.h"

namespace fastslide {
namespace formats {
namespace aperio {

/// @brief Metadata extracted from Aperio SVS image descriptions
struct AperioMetadata {
  aifocore::Size<double, 2> mpp = {0.0, 0.0};  ///< Microns per pixel (x, y)
  double app_mag = 0.0;                        ///< Apparent magnification
  std::string scanner_id;                      ///< Scanner ID string
};

/// @brief Parser for Aperio SVS metadata from TIFF image descriptions
class AperioMetadataParser {
 public:
  /// @brief Parse Aperio metadata from image description
  ///
  /// Extracts metadata from Aperio-formatted image description strings
  /// commonly found in SVS files. The parser looks for key-value pairs
  /// separated by '|' characters.
  ///
  /// @param description Image description string from TIFF
  /// @param metadata Output metadata structure to populate
  /// @return Status indicating success or failure with details
  static absl::Status ParseFromDescription(const std::string& description,
                                           AperioMetadata& metadata);

  /// @brief Check if an image description contains Aperio metadata
  /// @param description Image description string to check
  /// @return true if the description appears to contain Aperio metadata
  static bool IsAperioFormat(const std::string& description);

  /// @brief Parse associated image name from Aperio description
  ///
  /// Extracts the name of associated images (like thumbnails, labels)
  /// from Aperio image descriptions.
  ///
  /// @param description Image description string
  /// @return Associated image name, or empty string if not found
  static std::string ParseAssociatedImageName(const std::string& description);

 private:
  /// @brief Extract key-value pairs from pipe-separated description
  /// @param description Full description string
  /// @param key Key to search for
  /// @return Value associated with the key, or empty string if not found
  static std::string ExtractValue(const std::string& description,
                                  const std::string& key);
};

}  // namespace aperio
}  // namespace formats
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_METADATA_PARSER_H_
