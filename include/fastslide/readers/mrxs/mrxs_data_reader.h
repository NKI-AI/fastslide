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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DATA_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DATA_READER_H_

#include <cstdint>
#include <filesystem>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"

/// @file mrxs_data_reader.h
/// @brief MRXS data file reader and validation utilities
///
/// Provides utilities for reading compressed tile data from MRXS data files
/// (Dat_*.dat) and validating tile parameters. Centralizes all data file
/// operations and validation logic.

namespace fs = std::filesystem;

namespace fastslide {
namespace mrxs {

/// @brief Validator for tile data parameters
///
/// Provides static methods to validate tile parameters before reading to
/// prevent crashes from malformed data. This centralizes validation logic
/// that was previously duplicated across multiple locations.
class TileDataValidator {
 public:
  /// @brief Validate tile parameters
  ///
  /// Checks that tile offset, length, and derived values are valid and safe:
  /// - offset >= 0
  /// - length > 0 and <= kMaxTileSize
  /// - offset + length doesn't overflow
  ///
  /// @param tile Tile record to validate
  /// @return OkStatus if valid, error otherwise
  static absl::Status ValidateTileParams(const MiraxTileRecord& tile);

  /// @brief Validate file number is within range
  ///
  /// @param file_number File number from tile record
  /// @param num_datafiles Number of data files available
  /// @return OkStatus if valid, error otherwise
  static absl::Status ValidateFileNumber(int32_t file_number,
                                         size_t num_datafiles);
};

/// @brief Helper for reading MRXS data files
///
/// Provides methods to read compressed tile data from MRXS data files
/// with automatic validation and error handling. Uses RAII for file
/// handle management.
///
/// Note: This class intentionally doesn't cache file handles. File handle
/// pooling should be implemented at a higher level if needed for performance.
class MrxsDataReader {
 public:
  /// @brief Read raw tile data from a data file
  ///
  /// Opens the appropriate data file, seeks to the tile's offset, and reads
  /// the compressed image data. Validates tile parameters and file bounds
  /// before reading.
  ///
  /// @param dirname Directory containing MRXS files
  /// @param tile Tile metadata (offset, length, file number)
  /// @param datafile_paths Paths to data files
  /// @return Raw compressed data or error
  /// @retval absl::InvalidArgumentError if tile params are invalid
  /// @retval absl::NotFoundError if data file cannot be opened
  /// @retval absl::InternalError if seek or read fails
  static absl::StatusOr<std::vector<uint8_t>> ReadTileData(
      const fs::path& dirname, const MiraxTileRecord& tile,
      const std::vector<std::string>& datafile_paths);

  /// @brief Read data from a specific offset in a data file
  ///
  /// Generic method for reading arbitrary data from MRXS data files.
  /// Used for associated data, position buffers, etc.
  ///
  /// @param datafile_path Full path to data file
  /// @param offset Byte offset in file
  /// @param size Number of bytes to read
  /// @return Raw data or error
  static absl::StatusOr<std::vector<uint8_t>> ReadData(
      const fs::path& datafile_path, int64_t offset, int64_t size);
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DATA_READER_H_
