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

#include "fastslide/readers/mrxs/mrxs_data_reader.h"

#include <limits>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/mrxs/mrxs_constants.h"
#include "fastslide/runtime/io/file_reader.h"

namespace fastslide {
namespace mrxs {

aifocore::Status TileDataValidator::ValidateTileParams(
    const MiraxTileRecord& tile) {
  // Validate offset
  if (tile.offset < 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid negative offset {} for tile at ({}, {})",
                              tile.offset, tile.x, tile.y));
  }

  // Validate length
  if (tile.length <= 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid length {} for tile at ({}, {})",
                              tile.length, tile.x, tile.y));
  }

  // Prevent bad_alloc from unreasonably large allocations
  if (tile.length > constants::kMaxTileSize) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Tile length {} exceeds maximum allowed size of {} "
            "for tile at ({}, {})",
            tile.length, constants::kMaxTileSize, tile.x, tile.y));
  }

  // Validate that offset + length doesn't overflow
  const int64_t end_offset =
      static_cast<int64_t>(tile.offset) + static_cast<int64_t>(tile.length);
  if (end_offset < 0 ||
      end_offset > std::numeric_limits<int64_t>::max() - 1024) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Tile offset {} + length {} causes overflow for "
                              "tile at ({}, {})",
                              tile.offset, tile.length, tile.x, tile.y));
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status TileDataValidator::ValidateFileNumber(int32_t file_number,
                                                       size_t num_datafiles) {
  if (file_number < 0 || file_number >= static_cast<int32_t>(num_datafiles)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid file number: {} (must be 0-{})",
                              file_number, num_datafiles - 1));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Result<std::vector<uint8_t>> MrxsDataReader::ReadTileData(
    const fs::path& dirname, const MiraxTileRecord& tile,
    const std::vector<std::string>& datafile_paths) {
  // Validate file number
  AIFOCORE_RETURN_IF_ERROR(TileDataValidator::ValidateFileNumber(
      tile.data_file_number, datafile_paths.size()));

  // Validate tile parameters
  AIFOCORE_RETURN_IF_ERROR(TileDataValidator::ValidateTileParams(tile));

  // Construct full path to data file
  fs::path data_path = dirname / datafile_paths[tile.data_file_number];

  // Open file
  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(data_path, "rb"));

  // Get file size to validate read bounds
  int64_t file_size;
  AIFOCORE_ASSIGN_OR_RETURN(file_size, file.GetSize());

  // Validate that tile data fits within file bounds
  const int64_t end_offset =
      static_cast<int64_t>(tile.offset) + static_cast<int64_t>(tile.length);
  if (end_offset > file_size) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Tile data extends beyond file size: offset={}, "
            "length={}, end={}, file_size={} for tile at ({}, {})",
            tile.offset, tile.length, end_offset, file_size, tile.x, tile.y));
  }

  // Seek to tile
  AIFOCORE_RETURN_IF_ERROR(file.Seek(tile.offset));

  // Read tile data
  std::vector<uint8_t> data;
  AIFOCORE_ASSIGN_OR_RETURN(data, file.ReadBytes(tile.length));

  return data;
}

aifocore::Result<std::vector<uint8_t>> MrxsDataReader::ReadData(
    const fs::path& datafile_path, int64_t offset, int64_t size) {
  // Validate parameters
  if (offset < 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid negative offset: {}", offset));
  }

  if (size <= 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid size: {}", size));
  }

  // Open file
  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(datafile_path, "rb"));

  // Seek to offset
  AIFOCORE_RETURN_IF_ERROR(file.Seek(offset));

  // Read data
  std::vector<uint8_t> data;
  AIFOCORE_ASSIGN_OR_RETURN(data, file.ReadBytes(size));

  return data;
}

}  // namespace mrxs
}  // namespace fastslide
