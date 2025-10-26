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

/// @file mrxs_ini_parser.cpp
/// @brief Simple INI file parser for MRXS Slidedat.ini files
///
/// This parser handles the specific INI format used by 3DHISTECH MRXS slides:
/// - UTF-8 BOM (Byte Order Mark) at the start of the file
/// - Section headers: [SECTION_NAME]
/// - Key-value pairs: KEY=VALUE
/// - Comments starting with ; or #
/// - No support for multi-line values or complex quoting
///
/// The format is straightforward and this parser does not use a full INI library
/// to avoid unnecessary dependencies and maintain precise control over parsing.

#include "fastslide/readers/mrxs/mrxs_ini_parser.h"

#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"

namespace fastslide {
namespace mrxs {
namespace internal {

/// @brief Load and parse an INI file from disk
///
/// Reads an INI file and parses it into sections and key-value pairs. Handles
/// UTF-8 BOM (Byte Order Mark) commonly found in MRXS Slidedat.ini files.
/// Supports standard INI syntax with [SECTION] headers and KEY=VALUE pairs.
/// Comments starting with ; or # are ignored.
///
/// @param path Filesystem path to the INI file
/// @return StatusOr containing parsed IniFile object or error
/// @retval absl::NotFoundError if file cannot be opened
absl::StatusOr<IniFile> IniFile::Load(const fs::path& path) {
  std::ifstream file{path};
  if (!file.is_open()) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       absl::StrFormat("Cannot open file: %s", path.string()));
  }

  IniFile ini;
  std::string current_section;
  std::string line;
  bool first_line = true;

  while (std::getline(file, line)) {
    // Remove UTF-8 BOM from first line if present
    if (first_line && line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
      line = line.substr(3);
    }
    first_line = false;

    // Trim whitespace
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);

    // Skip empty lines and comments
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }

    // Check for section header
    if (line[0] == '[' && line.back() == ']') {
      current_section = line.substr(1, line.length() - 2);
      // Trim section name
      current_section.erase(0, current_section.find_first_not_of(" \t\r\n"));
      current_section.erase(current_section.find_last_not_of(" \t\r\n") + 1);
    } else {
      // Parse key=value pair
      size_t eq_pos = line.find('=');
      if (eq_pos != std::string::npos) {
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        // Trim key and value
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        ini.data_[current_section][key] = value;
      }
    }
  }

  return ini;
}

/// @brief Retrieve a string value from the INI file
///
/// Looks up a key within a specific section and returns its value as a string.
///
/// @param section Section name
/// @param key Key name within the section
/// @return StatusOr containing the string value or error
/// @retval absl::NotFoundError if section or key does not exist
absl::StatusOr<std::string> IniFile::GetString(std::string_view section,
                                               std::string_view key) const {
  auto section_it = data_.find(std::string(section));
  if (section_it == data_.end()) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       absl::StrFormat("Section not found: %s", section));
  }

  auto key_it = section_it->second.find(std::string(key));
  if (key_it == section_it->second.end()) {
    return MAKE_STATUS(
        absl::StatusCode::kNotFound,
        absl::StrFormat("Key not found: %s in section %s", key, section));
  }

  return key_it->second;
}

/// @brief Retrieve an integer value from the INI file
///
/// Looks up a key within a specific section, parses its value as an integer,
/// and returns it. Uses std::stoi for parsing.
///
/// @param section Section name
/// @param key Key name within the section
/// @return StatusOr containing the integer value or error
/// @retval absl::NotFoundError if section or key does not exist
/// @retval absl::InvalidArgumentError if value cannot be parsed as integer
absl::StatusOr<int> IniFile::GetInt(std::string_view section,
                                    std::string_view key) const {
  std::string str_result;
  ASSIGN_OR_RETURN(str_result, GetString(section, key));

  try {
    return std::stoi(str_result);
  } catch (const std::exception& e) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Cannot parse integer for key %s in section %s: %s",
                        key, section, e.what()));
  }
}

/// @brief Retrieve a floating-point value from the INI file
///
/// Looks up a key within a specific section, parses its value as a double,
/// and returns it. Uses std::stod for parsing.
///
/// @param section Section name
/// @param key Key name within the section
/// @return StatusOr containing the double value or error
/// @retval absl::NotFoundError if section or key does not exist
/// @retval absl::InvalidArgumentError if value cannot be parsed as double
absl::StatusOr<double> IniFile::GetDouble(std::string_view section,
                                          std::string_view key) const {
  std::string str_result;
  ASSIGN_OR_RETURN(str_result, GetString(section, key));

  try {
    return std::stod(str_result);
  } catch (const std::exception& e) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Cannot parse double for key %s in section %s: %s", key,
                        section, e.what()));
  }
}

/// @brief Check if a section exists in the INI file
///
/// @param section Section name to check
/// @return True if section exists, false otherwise
bool IniFile::HasSection(std::string_view section) const {
  return data_.find(std::string(section)) != data_.end();
}

}  // namespace internal
}  // namespace mrxs
}  // namespace fastslide
