// Copyright 2025 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in git liance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fastslide/readers/mrxs/mrxs_index_reader.h"

#include <cstring>
#include <limits>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/readers/mrxs/mrxs_constants.h"
#include "fastslide/runtime/io/binary_utils.h"

namespace fastslide {
namespace mrxs {

absl::StatusOr<MrxsIndexReader> MrxsIndexReader::Open(
    const fs::path& index_path, const SlideDataInfo& slide_info) {
  // Open index file
  FileReader file;
  ASSIGN_OR_RETURN(
      file, FileReader::Open(index_path, "rb"),
      absl::StrFormat("Cannot open index file: %s", index_path.string()));

  // Read and validate header
  std::tuple<int64_t, int64_t> roots;
  ASSIGN_OR_RETURN(roots, ReadHeader(file, slide_info),
                   "Failed to read index file header");

  auto [hierarchical_root, nonhier_root] = roots;

  return MrxsIndexReader(std::move(file), &slide_info, hierarchical_root,
                         nonhier_root);
}

absl::StatusOr<std::tuple<int64_t, int64_t>> MrxsIndexReader::ReadHeader(
    const FileReader& file, const SlideDataInfo& slide_info) {
  // Read version
  char version[constants::kIndexVersionSize + 1] = {0};
  RETURN_IF_ERROR(file.Read(version, constants::kIndexVersionSize),
                  "Failed to read index file version");

  if (std::string_view(version, constants::kIndexVersionSize) !=
      constants::kIndexVersion) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Unsupported index version: %s", version));
  }

  // Skip UUID/slide ID (variable length, read from INI file)
  const size_t uuid_length = slide_info.slide_id.length();
  std::vector<char> uuid_buffer(uuid_length);
  RETURN_IF_ERROR(file.Read(uuid_buffer.data(), uuid_length),
                  "Failed to read slide UUID from index file");

  // Read hierarchical root pointer
  int32_t hier_root_32;
  ASSIGN_OR_RETURN(hier_root_32, ReadLeInt32(file.Get()),
                   "Failed to read hierarchical root pointer");
  int64_t hierarchical_root = hier_root_32;

  // Calculate non-hierarchical root
  // Format: [version][uuid][hier_root][nonhier_root_offset]
  const int64_t nonhier_root = hierarchical_root + 4;

  return std::make_tuple(hierarchical_root, nonhier_root);
}

MrxsIndexReader::MrxsIndexReader(FileReader file,
                                 const SlideDataInfo* slide_info,
                                 int64_t hierarchical_root,
                                 int64_t nonhier_root)
    : file_(std::move(file)),
      slide_info_(slide_info),
      hierarchical_root_(hierarchical_root),
      nonhier_root_(nonhier_root) {}

absl::StatusOr<std::vector<MiraxTileRecord>> MrxsIndexReader::ReadLevelTiles(
    int level_index, const PyramidLevelParameters& level_params) {
  const int zoom_levels = static_cast<int>(slide_info_->zoom_levels.size());
  if (level_index < 0 || level_index >= zoom_levels) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid level index: %d", level_index));
  }

  // Navigate to level data
  const int64_t level_pointer_offset = hierarchical_root_ + (4 * level_index);
  RETURN_IF_ERROR(
      file_.Seek(level_pointer_offset),
      absl::StrFormat("Failed to seek to level %d pointer", level_index));

  // Read pointer to this level's zoom data
  int32_t zoom_level_data_pointer_32;
  ASSIGN_OR_RETURN(zoom_level_data_pointer_32, ReadLeInt32(file_.Get()),
                   "Failed to read zoom level data pointer");
  int64_t zoom_level_data_pointer = zoom_level_data_pointer_32;

  RETURN_IF_ERROR(file_.Seek(zoom_level_data_pointer),
                  "Failed to seek to zoom level data");

  // Read zoom level data structure: [sentinel_zero][data_pages_pointer]
  int32_t sentinel_value;
  ASSIGN_OR_RETURN(sentinel_value, ReadLeInt32(file_.Get()),
                   "Failed to read sentinel value");

  if (sentinel_value != 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat(
            "Expected sentinel value 0 at beginning of zoom data, got %d",
            sentinel_value));
  }

  int32_t data_pages_pointer_32;
  ASSIGN_OR_RETURN(data_pages_pointer_32, ReadLeInt32(file_.Get()),
                   "Failed to read data pages pointer");
  int64_t data_pages_pointer = data_pages_pointer_32;

  RETURN_IF_ERROR(file_.Seek(data_pages_pointer),
                  "Failed to seek to data pages");

  // Get slide dimensions
  const int total_images_horizontal = slide_info_->images_x;
  const SlideZoomLevel& zoom_level = slide_info_->zoom_levels[level_index];

  // Parse paged image records
  std::vector<MiraxTileRecord> tiles;

  while (true) {
    int32_t page_length_32;
    ASSIGN_OR_RETURN(page_length_32, ReadLeInt32(file_.Get()),
                     "Failed to read page length");
    int64_t page_length = page_length_32;

    int32_t next_page_pointer_32;
    ASSIGN_OR_RETURN(next_page_pointer_32, ReadLeInt32(file_.Get()),
                     "Failed to read next page pointer");
    int64_t next_page_pointer = next_page_pointer_32;

    // Process each image record in this page
    for (int record_idx = 0; record_idx < page_length; ++record_idx) {
      int32_t image_index_32;
      ASSIGN_OR_RETURN(image_index_32, ReadLeInt32(file_.Get()),
                       "Failed to read image index");
      int64_t image_index = image_index_32;

      int32_t data_offset_32;
      ASSIGN_OR_RETURN(data_offset_32, ReadLeInt32(file_.Get()),
                       "Failed to read data offset");
      int64_t data_offset = data_offset_32;

      int32_t data_length_32;
      ASSIGN_OR_RETURN(data_length_32, ReadLeInt32(file_.Get()),
                       "Failed to read data length");
      int64_t data_length = data_length_32;

      int32_t data_file_number_32;
      ASSIGN_OR_RETURN(data_file_number_32, ReadLeInt32(file_.Get()),
                       "Failed to read data file number");
      int64_t data_file_number = data_file_number_32;

      // Validate tile data parameters
      if (data_offset < 0) {
        return MAKE_STATUS(
            absl::StatusCode::kInvalidArgument,
            absl::StrFormat("Invalid negative data offset %d for image index "
                            "%d at level %d",
                            data_offset, image_index, level_index));
      }

      if (data_length <= 0) {
        return MAKE_STATUS(
            absl::StatusCode::kInvalidArgument,
            absl::StrFormat(
                "Invalid data length %d for image index %d at level %d",
                data_length, image_index, level_index));
      }

      // Prevent bad_alloc from unreasonably large tile sizes
      if (data_length > constants::kMaxTileSize) {
        return MAKE_STATUS(
            absl::StatusCode::kInvalidArgument,
            absl::StrFormat("Data length %d exceeds maximum allowed size of %d "
                            "for image index %d at level %d",
                            data_length, constants::kMaxTileSize, image_index,
                            level_index));
      }

      // Validate that offset + length doesn't overflow int64
      const int64_t end_offset = data_offset + data_length;
      if (end_offset < 0 ||
          end_offset > std::numeric_limits<int64_t>::max() - 1024) {
        return MAKE_STATUS(
            absl::StatusCode::kInvalidArgument,
            absl::StrFormat("Data offset %d + length %d causes overflow for "
                            "image index %d at level %d",
                            data_offset, data_length, image_index,
                            level_index));
      }

      // Convert linear image_index to 2D grid coordinates
      const int32_t image_grid_x =
          static_cast<int32_t>(image_index % total_images_horizontal);
      const int32_t image_grid_y =
          static_cast<int32_t>(image_index / total_images_horizontal);

      // Subdivide image into multiple tiles (reduces nesting)
      auto subtiles = SubdivideImage(image_index, image_grid_x, image_grid_y,
                                     data_offset, data_length, data_file_number,
                                     level_index, level_params, zoom_level);
      tiles.insert(tiles.end(), subtiles.begin(), subtiles.end());
    }

    // Check if there are more pages
    if (next_page_pointer == 0) {
      break;  // End of page list
    }

    // Navigate to next page
    RETURN_IF_ERROR(file_.Seek(next_page_pointer),
                    "Failed to seek to next page");
  }

  return tiles;
}

std::vector<MiraxTileRecord> MrxsIndexReader::SubdivideImage(
    int64_t image_index, int32_t image_grid_x, int32_t image_grid_y,
    int64_t data_offset, int64_t data_length, int64_t data_file_number,
    int level_index, const PyramidLevelParameters& level_params,
    const SlideZoomLevel& zoom_level) {

  std::vector<MiraxTileRecord> tiles;

  const int total_images_horizontal = slide_info_->images_x;
  const int total_images_vertical = slide_info_->images_y;
  const int camera_image_divisions = slide_info_->image_divisions;

  const double sub_tile_width = static_cast<double>(zoom_level.image_width) /
                                level_params.subtiles_per_stored_image;
  const double sub_tile_height = static_cast<double>(zoom_level.image_height) /
                                 level_params.subtiles_per_stored_image;

  // Create tiles for each subdivision of this image
  for (int sub_tile_y_idx = 0;
       sub_tile_y_idx < level_params.subtiles_per_stored_image;
       sub_tile_y_idx++) {
    const int tile_grid_y =
        image_grid_y + (sub_tile_y_idx * camera_image_divisions);
    if (tile_grid_y >= total_images_vertical) {
      break;  // Outside slide bounds
    }

    for (int sub_tile_x_idx = 0;
         sub_tile_x_idx < level_params.subtiles_per_stored_image;
         sub_tile_x_idx++) {
      const int tile_grid_x =
          image_grid_x + (sub_tile_x_idx * camera_image_divisions);
      if (tile_grid_x >= total_images_horizontal) {
        break;  // Outside slide bounds
      }

      // Create tile metadata
      MiraxTileRecord tile;
      tile.image_index = static_cast<int32_t>(image_index);
      tile.offset = static_cast<int32_t>(data_offset);
      tile.length = static_cast<int32_t>(data_length);
      tile.data_file_number = static_cast<int32_t>(data_file_number);
      tile.x = tile_grid_x;
      tile.y = tile_grid_y;
      tile.subregion_x = sub_tile_width * sub_tile_x_idx;
      tile.subregion_y = sub_tile_height * sub_tile_y_idx;

      // Assign intensity gain value based on camera position
      if (!slide_info_->camera_position_gains.empty()) {
        const int camera_x = tile_grid_x / camera_image_divisions;
        const int camera_y = tile_grid_y / camera_image_divisions;
        const int positions_x =
            total_images_horizontal / camera_image_divisions;
        const int camera_pos_index = camera_y * positions_x + camera_x;

        if (camera_pos_index >= 0 &&
            camera_pos_index <
                static_cast<int>(slide_info_->camera_position_gains.size())) {
          tile.gain = slide_info_->camera_position_gains[camera_pos_index];
        }
      }

      tiles.push_back(tile);
    }
  }

  return tiles;
}

absl::StatusOr<NonHierRecordData> MrxsIndexReader::ReadNonHierRecord(
    int record_index) {
  // Navigate to non-hierarchical root
  RETURN_IF_ERROR(file_.Seek(nonhier_root_),
                  "Cannot seek to non-hierarchical root");

  // Read pointer to record pointer array
  int32_t record_array_pointer_32;
  ASSIGN_OR_RETURN(record_array_pointer_32, ReadLeInt32(file_.Get()),
                   "Cannot read non-hierarchical array pointer");
  int64_t record_array_pointer = record_array_pointer_32;

  // Navigate to the specific record's pointer
  const int64_t record_pointer_offset = record_array_pointer + 4 * record_index;
  RETURN_IF_ERROR(file_.Seek(record_pointer_offset),
                  absl::StrFormat("Cannot seek to record pointer for record %d",
                                  record_index));

  // Read pointer to record header
  int32_t record_header_pointer_32;
  ASSIGN_OR_RETURN(record_header_pointer_32, ReadLeInt32(file_.Get()),
                   "Cannot read record header pointer");
  int64_t record_header_pointer = record_header_pointer_32;

  // Navigate to record header
  RETURN_IF_ERROR(file_.Seek(record_header_pointer),
                  "Cannot seek to record header");

  // Read sentinel value (should be 0)
  int32_t sentinel_value;
  ASSIGN_OR_RETURN(sentinel_value, ReadLeInt32(file_.Get()),
                   "Cannot read sentinel value");

  if (sentinel_value != 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat(
            "Expected sentinel value 0 at beginning of non-hierarchical "
            "record, got %d",
            sentinel_value));
  }

  // Read pointer to data page
  int32_t data_page_pointer_32;
  ASSIGN_OR_RETURN(data_page_pointer_32, ReadLeInt32(file_.Get()),
                   "Cannot read data page pointer");
  int64_t data_page_pointer = data_page_pointer_32;

  // Navigate to data page
  RETURN_IF_ERROR(file_.Seek(data_page_pointer), "Cannot seek to data page");

  // Read page length
  int32_t page_length_32;
  ASSIGN_OR_RETURN(page_length_32, ReadLeInt32(file_.Get()),
                   "Cannot read page length");
  int64_t page_length = page_length_32;

  if (page_length < 1) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        "Expected at least one item in non-hierarchical data page");
  }

  // Skip metadata: next page pointer + two reserved fields
  for (int i = 0; i < 3; ++i) {
    int32_t skip;
    ASSIGN_OR_RETURN(skip, ReadLeInt32(file_.Get()), "Cannot read skip value");
    (void)skip;  // Intentionally unused
  }

  // Read actual data location
  int32_t data_offset_32;
  ASSIGN_OR_RETURN(data_offset_32, ReadLeInt32(file_.Get()),
                   "Cannot read data offset");
  int64_t data_offset = data_offset_32;

  int32_t data_size_32;
  ASSIGN_OR_RETURN(data_size_32, ReadLeInt32(file_.Get()),
                   "Cannot read data size");
  int64_t data_size = data_size_32;

  int32_t datafile_number_32;
  ASSIGN_OR_RETURN(datafile_number_32, ReadLeInt32(file_.Get()),
                   "Cannot read datafile number");
  int64_t datafile_number = datafile_number_32;

  // Validate datafile number
  if (datafile_number < 0 ||
      datafile_number >= static_cast<int>(slide_info_->datafile_paths.size())) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Invalid datafile number: %d (must be 0-%zu)",
                        datafile_number,
                        slide_info_->datafile_paths.size() - 1));
  }

  NonHierRecordData result;
  result.datafile_path = slide_info_->datafile_paths[datafile_number];
  result.offset = data_offset;
  result.size = data_size;

  return result;
}

}  // namespace mrxs
}  // namespace fastslide
