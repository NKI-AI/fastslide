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

#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/readers/mrxs/mrxs_constants.h"
#include "fastslide/runtime/io/file_reader.h"

namespace fastslide {
namespace mrxs {

absl::Status TileDataValidator::ValidateTileParams(
    const MiraxTileRecord& tile) {
  // Validate offset
  if (tile.offset < 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Invalid negative offset %d for tile at (%d, %d)",
                        tile.offset, tile.x, tile.y));
  }

  // Validate length
  if (tile.length <= 0) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid length %d for tile at (%d, %d)",
                                       tile.length, tile.x, tile.y));
  }

  // Prevent bad_alloc from unreasonably large allocations
  if (tile.length > constants::kMaxTileSize) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Tile length %d exceeds maximum allowed size of %d "
                        "for tile at (%d, %d)",
                        tile.length, constants::kMaxTileSize, tile.x, tile.y));
  }

  // Validate that offset + length doesn't overflow
  const int64_t end_offset =
      static_cast<int64_t>(tile.offset) + static_cast<int64_t>(tile.length);
  if (end_offset < 0 ||
      end_offset > std::numeric_limits<int64_t>::max() - 1024) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Tile offset %d + length %d causes overflow for "
                        "tile at (%d, %d)",
                        tile.offset, tile.length, tile.x, tile.y));
  }

  return absl::OkStatus();
}

absl::Status TileDataValidator::ValidateFileNumber(int32_t file_number,
                                                   size_t num_datafiles) {
  if (file_number < 0 || file_number >= static_cast<int32_t>(num_datafiles)) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Invalid file number: %d (must be 0-%zu)", file_number,
                        num_datafiles - 1));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> MrxsDataReader::ReadTileData(
    const fs::path& dirname, const MiraxTileRecord& tile,
    const std::vector<std::string>& datafile_paths) {
  // Validate file number
  RETURN_IF_ERROR(TileDataValidator::ValidateFileNumber(tile.data_file_number,
                                                        datafile_paths.size()),
                  "Invalid data file number");

  // Validate tile parameters
  RETURN_IF_ERROR(TileDataValidator::ValidateTileParams(tile),
                  "Invalid tile parameters");

  // Construct full path to data file
  fs::path data_path = dirname / datafile_paths[tile.data_file_number];

  // Open file
  FileReader file;
  ASSIGN_OR_RETURN(
      file, FileReader::Open(data_path, "rb"),
      absl::StrFormat("Cannot open data file: %s", data_path.string()));

  // Get file size to validate read bounds
  int64_t file_size;
  ASSIGN_OR_RETURN(file_size, file.GetSize(),
                   "Failed to determine data file size");

  // Validate that tile data fits within file bounds
  const int64_t end_offset =
      static_cast<int64_t>(tile.offset) + static_cast<int64_t>(tile.length);
  if (end_offset > file_size) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat(
            "Tile data extends beyond file size: offset=%d, "
            "length=%d, end=%lld, file_size=%lld for tile at (%d, %d)",
            tile.offset, tile.length, end_offset, file_size, tile.x, tile.y));
  }

  // Seek to tile
  RETURN_IF_ERROR(file.Seek(tile.offset), "Failed to seek in data file");

  // Read tile data
  std::vector<uint8_t> data;
  ASSIGN_OR_RETURN(data, file.ReadBytes(tile.length),
                   "Failed to read tile data");

  return data;
}

absl::StatusOr<std::vector<uint8_t>> MrxsDataReader::ReadData(
    const fs::path& datafile_path, int64_t offset, int64_t size) {
  // Validate parameters
  if (offset < 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Invalid negative offset: %lld", offset));
  }

  if (size <= 0) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid size: %lld", size));
  }

  // Open file
  FileReader file;
  ASSIGN_OR_RETURN(
      file, FileReader::Open(datafile_path, "rb"),
      absl::StrFormat("Cannot open data file: %s", datafile_path.string()));

  // Seek to offset
  RETURN_IF_ERROR(file.Seek(offset),
                  absl::StrFormat("Cannot seek to offset %lld", offset));

  // Read data
  std::vector<uint8_t> data;
  ASSIGN_OR_RETURN(data, file.ReadBytes(size),
                   absl::StrFormat("Cannot read %lld bytes", size));

  return data;
}

}  // namespace mrxs
}  // namespace fastslide
