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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INI_PARSER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INI_PARSER_H_

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace fs = std::filesystem;

namespace fastslide {
namespace mrxs {
namespace internal {

/// @brief Simple INI file parser for MRXS Slidedat.ini files
///
/// This class handles the specific format used by MRXS files, including:
/// - UTF-8 BOM at start of file
/// - Hierarchical sections with [SECTION] markers
/// - Key=Value pairs
/// - Comments starting with ; or #
class IniFile {
 public:
  /// @brief Load and parse an INI file
  /// @param path Path to the INI file
  /// @return StatusOr containing parsed IniFile or error
  static absl::StatusOr<IniFile> Load(const fs::path& path);

  /// @brief Get string value from section
  /// @param section Section name
  /// @param key Key name
  /// @return StatusOr containing value or error
  absl::StatusOr<std::string> GetString(std::string_view section,
                                        std::string_view key) const;

  /// @brief Get integer value from section
  /// @param section Section name
  /// @param key Key name
  /// @return StatusOr containing integer value or error
  absl::StatusOr<int> GetInt(std::string_view section,
                             std::string_view key) const;

  /// @brief Get double value from section
  /// @param section Section name
  /// @param key Key name
  /// @return StatusOr containing double value or error
  absl::StatusOr<double> GetDouble(std::string_view section,
                                   std::string_view key) const;

  /// @brief Check if section exists
  /// @param section Section name
  /// @return True if section exists
  bool HasSection(std::string_view section) const;

 private:
  std::map<std::string, std::map<std::string, std::string>> data_;
};

}  // namespace internal
}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INI_PARSER_H_
