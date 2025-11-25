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

#include <vector>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "aifocore/status/status_macros.h"

namespace fastslide {
namespace formats {
namespace aperio {

absl::Status AperioMetadataParser::ParseFromDescription(
    const std::string& description, AperioMetadata& metadata) {

  if (!IsAperioFormat(description)) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Not an Aperio SVS file: missing Aperio signature");
  }

  bool found_any_metadata = false;

  // Parse MPP value
  std::string mpp_str = ExtractValue(description, "MPP");
  if (!mpp_str.empty()) {
    double mpp_val;
    if (absl::SimpleAtod(mpp_str, &mpp_val) && mpp_val > 0.0) {
      metadata.mpp = {mpp_val, mpp_val};
      found_any_metadata = true;
    }
  }

  // Parse apparent magnification
  std::string app_mag_str = ExtractValue(description, "AppMag");
  if (!app_mag_str.empty()) {
    if (absl::SimpleAtod(app_mag_str, &metadata.app_mag)) {
      found_any_metadata = true;
    }
  }

  // Extract scanner ID
  metadata.scanner_id = ExtractValue(description, "ScanScope ID");
  if (!metadata.scanner_id.empty()) {
    found_any_metadata = true;
  }

  if (!found_any_metadata) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       "No valid Aperio metadata found in description");
  }

  return absl::OkStatus();
}

bool AperioMetadataParser::IsAperioFormat(const std::string& description) {
  // Check for Aperio signature at the beginning of the description
  return description.starts_with("Aperio") ||
         description.find("Aperio") != std::string::npos;
}

std::string AperioMetadataParser::ParseAssociatedImageName(
    const std::string& description) {

  // Look for common associated image patterns in Aperio files
  if (description.find("macro") != std::string::npos ||
      description.find("Macro") != std::string::npos) {
    return "macro";
  }

  if (description.find("thumbnail") != std::string::npos ||
      description.find("Thumbnail") != std::string::npos) {
    return "thumbnail";
  }

  if (description.find("label") != std::string::npos ||
      description.find("Label") != std::string::npos) {
    return "label";
  }

  // Try to extract name from description format
  // Look for pattern like "name - description"
  std::vector<std::string> parts = absl::StrSplit(description, " - ");
  if (parts.size() >= 2 && !parts[0].empty()) {
    return parts[0];
  }

  return "";
}

std::string AperioMetadataParser::ExtractValue(const std::string& description,
                                               const std::string& key) {
  // Aperio metadata is typically pipe-separated
  std::vector<std::string> parts = absl::StrSplit(description, '|');

  for (const auto& part : parts) {
    // Look for key=value or key = value patterns
    size_t eq_pos = part.find('=');
    if (eq_pos != std::string::npos) {
      std::string part_key = part.substr(0, eq_pos);
      std::string part_value = part.substr(eq_pos + 1);

      // Trim whitespace from key and value
      part_key.erase(0, part_key.find_first_not_of(" \t"));
      part_key.erase(part_key.find_last_not_of(" \t") + 1);
      part_value.erase(0, part_value.find_first_not_of(" \t"));
      part_value.erase(part_value.find_last_not_of(" \t") + 1);

      if (part_key == key) {
        return part_value;
      }
    }
  }

  return "";
}

}  // namespace aperio
}  // namespace formats
}  // namespace fastslide
