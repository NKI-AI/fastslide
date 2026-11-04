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
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/runtime/io/file_reader.h"

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
  static aifocore::Status ValidateTileParams(const MiraxTileRecord& tile);

  /// @brief Validate file number is within range
  ///
  /// @param file_number File number from tile record
  /// @param num_datafiles Number of data files available
  /// @return OkStatus if valid, error otherwise
  static aifocore::Status ValidateFileNumber(int32_t file_number,
                                             size_t num_datafiles);
};

/// @brief The open Data files of one slide
///
/// Each Data file is opened on first use and kept open until this object is
/// destroyed, so reading a tile costs one positional read instead of an
/// open/close round trip (which dominates on network file systems). Owned by
/// the reader, so handles are released with the slide. Reads are thread-safe.
class MrxsDataFiles {
 public:
  /// @param dirname Directory containing MRXS files
  /// @param datafile_paths Data file names, relative to @p dirname
  MrxsDataFiles(fs::path dirname, std::vector<std::string> datafile_paths)
      : dirname_(std::move(dirname)),
        datafile_paths_(std::move(datafile_paths)),
        files_(datafile_paths_.size()) {}

  /// @brief Read raw tile data, validating tile parameters and file bounds
  /// @retval InvalidArgument if tile params are invalid or out of bounds
  /// @retval NotFound if the data file cannot be opened
  /// @retval Internal if the read fails
  aifocore::Result<std::vector<uint8_t>> ReadTile(
      const MiraxTileRecord& tile) const;

 private:
  struct OpenFile {
    FileReader reader;
    int64_t size;
  };

  aifocore::Result<const OpenFile*> Get(size_t file_number) const;

  fs::path dirname_;
  std::vector<std::string> datafile_paths_;
  mutable std::mutex mutex_;
  mutable std::vector<std::unique_ptr<OpenFile>> files_;  ///< guarded by mutex_
};

/// @brief Helper for one-off reads from MRXS data files
class MrxsDataReader {
 public:
  /// @brief Read data from a specific offset in a data file
  ///
  /// Generic method for reading arbitrary data from MRXS data files.
  /// Used for associated data, position buffers, etc.
  ///
  /// @param datafile_path Full path to data file
  /// @param offset Byte offset in file
  /// @param size Number of bytes to read
  /// @return Raw data or error
  static aifocore::Result<std::vector<uint8_t>> ReadData(
      const fs::path& datafile_path, int64_t offset, int64_t size);
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DATA_READER_H_
