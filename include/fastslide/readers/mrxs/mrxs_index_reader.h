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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INDEX_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INDEX_READER_H_

#include <cstdint>
#include <filesystem>
#include <tuple>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/runtime/io/file_reader.h"

/// @file mrxs_index_reader.h
/// @brief MRXS index file reader helper class
///
/// Encapsulates all operations related to reading and parsing the MRXS
/// Index.dat file. This file contains the binary index mapping tile grid
/// coordinates to compressed image data locations in the data files.
///
/// Separating index file operations into this helper class:
/// - Reduces complexity in MrxsReader main class
/// - Enables focused unit testing of index parsing logic
/// - Provides clear API for index file operations
/// - Centralizes index file format knowledge

namespace fs = std::filesystem;

namespace fastslide {
namespace mrxs {

/// @brief Result of reading a non-hierarchical record
struct NonHierRecordData {
  std::string datafile_path;  ///< Relative path to data file
  int64_t offset;             ///< Byte offset in data file
  int64_t size;               ///< Size in bytes
};

/// @brief Helper class for reading MRXS Index.dat file
///
/// This class encapsulates all index file operations and maintains the
/// file handle using RAII. It provides methods to read:
/// - Hierarchical tile records (multi-resolution pyramid)
/// - Non-hierarchical records (associated data)
/// - Camera position and gain data
///
/// Usage:
/// ```cpp
/// ASSIGN_OR_RETURN(auto reader,
///                  MrxsIndexReader::Open(index_path, slide_info));
/// ASSIGN_OR_RETURN(auto tiles, reader.ReadLevelTiles(0));
/// ```
class MrxsIndexReader {
 public:
  /// @brief Default constructor (creates invalid reader)
  MrxsIndexReader() = default;

  /// @brief Factory method to open an MRXS index file
  ///
  /// @param index_path Path to Index.dat file
  /// @param slide_info Slide information (for validation and context)
  /// @return MrxsIndexReader instance or error
  /// @retval absl::NotFoundError if file cannot be opened
  /// @retval absl::InvalidArgumentError if file header is invalid
  static absl::StatusOr<MrxsIndexReader> Open(const fs::path& index_path,
                                              const SlideDataInfo& slide_info);

  /// @brief Move constructor
  MrxsIndexReader(MrxsIndexReader&& other) noexcept = default;

  /// @brief Move assignment
  MrxsIndexReader& operator=(MrxsIndexReader&& other) noexcept = default;

  /// @brief Destructor (automatic cleanup via FileReader)
  ~MrxsIndexReader() = default;

  /// @brief Deleted copy constructor (unique ownership)
  MrxsIndexReader(const MrxsIndexReader&) = delete;

  /// @brief Deleted copy assignment (unique ownership)
  MrxsIndexReader& operator=(const MrxsIndexReader&) = delete;

  /// @brief Read all tile records for a specific pyramid level
  ///
  /// Parses the hierarchical section of the index file to extract tile
  /// metadata for the requested level. Handles tile subdivision for levels
  /// where subtiles_per_stored_image > 1.
  ///
  /// @param level_index Zero-based level index (0 = highest resolution)
  /// @param level_params Pyramid parameters for this level
  /// @return Vector of tile records or error
  /// @retval absl::InvalidArgumentError if level index is invalid or data is
  /// malformed
  /// @retval absl::InternalError if file I/O fails
  absl::StatusOr<std::vector<MiraxTileRecord>> ReadLevelTiles(
      int level_index, const PyramidLevelParameters& level_params);

  /// @brief Read a non-hierarchical record (associated data)
  ///
  /// Reads metadata for non-hierarchical data items like labels, macros,
  /// position data, and XML metadata.
  ///
  /// @param record_index Record index in non-hierarchical section
  /// @return Record data (path, offset, size) or error
  /// @retval absl::InvalidArgumentError if record index is invalid
  /// @retval absl::InternalError if file I/O fails
  absl::StatusOr<NonHierRecordData> ReadNonHierRecord(int record_index);

 private:
  /// @brief Private constructor (use Open factory method)
  MrxsIndexReader(FileReader file, const SlideDataInfo* slide_info,
                  int64_t hierarchical_root, int64_t nonhier_root);

  /// @brief Read index file header and validate
  ///
  /// @param file File reader positioned at start of file
  /// @param slide_info Slide info for validation
  /// @return Tuple of (hierarchical_root, nonhier_root) offsets or error
  static absl::StatusOr<std::tuple<int64_t, int64_t>> ReadHeader(
      const FileReader& file, const SlideDataInfo& slide_info);

  /// @brief Subdivide a stored image into multiple logical tiles
  ///
  /// When subtiles_per_stored_image > 1, each stored image contains multiple
  /// tiles that need to be extracted as separate sub-regions. This method
  /// creates tile records for all subtiles within a stored image.
  ///
  /// @param image_index Linear index of source image
  /// @param image_grid_x X coordinate in image grid
  /// @param image_grid_y Y coordinate in image grid
  /// @param data_offset Offset in data file
  /// @param data_length Size in bytes
  /// @param data_file_number Data file index
  /// @param level_index Pyramid level index
  /// @param level_params Level parameters
  /// @param zoom_level Zoom level metadata
  /// @return Vector of tile records for all subtiles
  std::vector<MiraxTileRecord> SubdivideImage(
      int64_t image_index, int32_t image_grid_x, int32_t image_grid_y,
      int64_t data_offset, int64_t data_length, int64_t data_file_number,
      int level_index, const PyramidLevelParameters& level_params,
      const SlideZoomLevel& zoom_level);

  FileReader file_;                  ///< Index file handle (RAII)
  const SlideDataInfo* slide_info_;  ///< Slide information pointer
  int64_t hierarchical_root_ = 0;    ///< Offset to hierarchical root
  int64_t nonhier_root_ = 0;         ///< Offset to non-hierarchical root
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INDEX_READER_H_
