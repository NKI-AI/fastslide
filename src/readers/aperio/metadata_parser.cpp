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

#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"

namespace fastslide {
namespace formats {
namespace aperio {

namespace {
// Helper to split string by delimiter
std::vector<std::string> SplitString(std::string_view str,
                                     std::string_view delimiter) {
  std::vector<std::string> parts;
  size_t start = 0;
  size_t end = str.find(delimiter);

  while (end != std::string_view::npos) {
    parts.emplace_back(str.substr(start, end - start));
    start = end + delimiter.length();
    end = str.find(delimiter, start);
  }
  parts.emplace_back(str.substr(start));
  return parts;
}

// Helper to split string by character delimiter
std::vector<std::string> SplitString(std::string_view str, char delimiter) {
  std::vector<std::string> parts;
  size_t start = 0;
  size_t end = str.find(delimiter);

  while (end != std::string_view::npos) {
    parts.emplace_back(str.substr(start, end - start));
    start = end + 1;
    end = str.find(delimiter, start);
  }
  parts.emplace_back(str.substr(start));
  return parts;
}

// Helper for string to double conversion
bool ParseDouble(const std::string& str, double& value) {
  if (str.empty()) {
    return false;
  }
  char* end_ptr;
  value = std::strtod(str.c_str(), &end_ptr);
  return end_ptr != str.c_str();
}
}  // namespace

aifocore::Status AperioMetadataParser::ParseFromDescription(
    const std::string& description, AperioMetadata& metadata) {

  if (!IsAperioFormat(description)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Not an Aperio SVS file: missing Aperio signature");
  }

  bool found_any_metadata = false;

  // Parse MPP value
  std::string mpp_str = ExtractValue(description, "MPP");
  if (!mpp_str.empty()) {
    double mpp_val;
    if (ParseDouble(mpp_str, mpp_val) && mpp_val > 0.0) {
      metadata.mpp = {mpp_val, mpp_val};
      found_any_metadata = true;
    }
  }

  // Parse apparent magnification
  std::string app_mag_str = ExtractValue(description, "AppMag");
  if (!app_mag_str.empty()) {
    if (ParseDouble(app_mag_str, metadata.app_mag)) {
      found_any_metadata = true;
    }
  }

  // Extract scanner ID
  metadata.scanner_id = ExtractValue(description, "ScanScope ID");
  if (!metadata.scanner_id.empty()) {
    found_any_metadata = true;
  }

  if (!found_any_metadata) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        "No valid Aperio metadata found in description");
  }

  return aifocore::Status::OkStatus();
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
  std::vector<std::string> parts = SplitString(description, " - ");
  if (parts.size() >= 2 && !parts[0].empty()) {
    return parts[0];
  }

  return "";
}

std::string AperioMetadataParser::ExtractValue(const std::string& description,
                                               const std::string& key) {
  // Aperio metadata is typically pipe-separated
  std::vector<std::string> parts = SplitString(description, '|');

  for (const auto& part : parts) {
    // Look for key=value or key = value patterns
    size_t eq_pos = part.find('=');
    if (eq_pos != std::string::npos) {
      std::string part_key = part.substr(0, eq_pos);
      std::string part_value = part.substr(eq_pos + 1);

      // Trim whitespace from key and value
      part_key.erase(0, part_key.find_first_not_of(" \t"));
      size_t key_end = part_key.find_last_not_of(" \t");
      if (key_end != std::string::npos) {
        part_key.erase(key_end + 1);
      } else if (part_key.find_first_not_of(" \t") == std::string::npos) {
        part_key.clear();  // All whitespace
      }

      part_value.erase(0, part_value.find_first_not_of(" \t"));
      size_t val_end = part_value.find_last_not_of(" \t");
      if (val_end != std::string::npos) {
        part_value.erase(val_end + 1);
      } else if (part_value.find_first_not_of(" \t") == std::string::npos) {
        part_value.clear();  // All whitespace
      }

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
