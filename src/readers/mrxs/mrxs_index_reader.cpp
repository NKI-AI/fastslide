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

#include "fastslide/readers/mrxs/mrxs_index_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/mrxs/mrxs_constants.h"
#include "fastslide/runtime/io/binary_utils.h"

namespace fastslide {
namespace mrxs {

aifocore::Result<MrxsIndexReader> MrxsIndexReader::Open(
    const fs::path& index_path, const SlideDataInfo& slide_info) {
  // Open index file
  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(index_path, "rb"));

  // Read and validate header
  std::tuple<int64_t, int64_t> roots;
  AIFOCORE_ASSIGN_OR_RETURN(roots, ReadHeader(file, slide_info));

  auto [hierarchical_root, nonhier_root] = roots;

  return MrxsIndexReader(std::move(file), &slide_info, hierarchical_root,
                         nonhier_root);
}

aifocore::Result<std::tuple<int64_t, int64_t>> MrxsIndexReader::ReadHeader(
    const FileReader& file, const SlideDataInfo& slide_info) {
  // Read version
  char version[constants::kIndexVersionSize + 1] = {0};
  AIFOCORE_RETURN_IF_ERROR(file.Read(version, constants::kIndexVersionSize));

  if (std::string_view(version, constants::kIndexVersionSize) !=
      constants::kIndexVersion) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Unsupported index version: {}", version));
  }

  // Skip UUID/slide ID (variable length, read from INI file)
  const size_t uuid_length = slide_info.slide_id.length();
  std::vector<char> uuid_buffer(uuid_length);
  AIFOCORE_RETURN_IF_ERROR(file.Read(uuid_buffer.data(), uuid_length));

  // The header layout is:
  //   [version][uuid][hier_root_ptr][nonhier_root_ptr]
  // where each *_root_ptr is a 32-bit little-endian file offset pointing
  // at the start of the corresponding pointer array. Both arrays are then
  // indexed via `array_start + 4 * index`.
  int32_t hier_root_value_32;
  AIFOCORE_ASSIGN_OR_RETURN(hier_root_value_32, ReadLeInt32(file.Get()));
  int64_t hier_root_array_start = hier_root_value_32;

  int32_t nonhier_root_value_32;
  AIFOCORE_ASSIGN_OR_RETURN(nonhier_root_value_32, ReadLeInt32(file.Get()));
  int64_t nonhier_root_array_start = nonhier_root_value_32;

  return std::make_tuple(hier_root_array_start, nonhier_root_array_start);
}

MrxsIndexReader::MrxsIndexReader(FileReader file,
                                 const SlideDataInfo* slide_info,
                                 int64_t hierarchical_root,
                                 int64_t nonhier_root)
    : file_(std::move(file)),
      slide_info_(slide_info),
      hierarchical_root_(hierarchical_root),
      nonhier_root_(nonhier_root) {}

aifocore::Result<std::vector<MrxsIndexReader::RawIndexRecord>>
MrxsIndexReader::ReadLevelRecords(int64_t level_pointer_in_root) {
  std::vector<RawIndexRecord> records;

  AIFOCORE_RETURN_IF_ERROR(file_.Seek(level_pointer_in_root));

  // Read pointer to this (hierarchy, level)'s data block.
  int32_t zoom_level_data_pointer_32;
  AIFOCORE_ASSIGN_OR_RETURN(zoom_level_data_pointer_32,
                            ReadLeInt32(file_.Get()));
  int64_t zoom_level_data_pointer = zoom_level_data_pointer_32;

  // A zero pointer means this (hierarchy, level) carries no data: bail
  // out cleanly so callers can skip it without raising a hard error.
  if (zoom_level_data_pointer == 0) {
    return records;
  }

  AIFOCORE_RETURN_IF_ERROR(file_.Seek(zoom_level_data_pointer));

  // Read level data block header: [sentinel_zero][data_pages_pointer].
  int32_t sentinel_value;
  AIFOCORE_ASSIGN_OR_RETURN(sentinel_value, ReadLeInt32(file_.Get()));
  if (sentinel_value != 0) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Expected sentinel value 0 at beginning of zoom data, got {}",
            sentinel_value));
  }

  int32_t data_pages_pointer_32;
  AIFOCORE_ASSIGN_OR_RETURN(data_pages_pointer_32, ReadLeInt32(file_.Get()));
  int64_t data_pages_pointer = data_pages_pointer_32;
  if (data_pages_pointer == 0) {
    return records;
  }

  AIFOCORE_RETURN_IF_ERROR(file_.Seek(data_pages_pointer));

  while (true) {
    int32_t page_length_32;
    AIFOCORE_ASSIGN_OR_RETURN(page_length_32, ReadLeInt32(file_.Get()));
    int64_t page_length = page_length_32;

    int32_t next_page_pointer_32;
    AIFOCORE_ASSIGN_OR_RETURN(next_page_pointer_32, ReadLeInt32(file_.Get()));
    int64_t next_page_pointer = next_page_pointer_32;

    for (int record_idx = 0; record_idx < page_length; ++record_idx) {
      int32_t image_index_32;
      AIFOCORE_ASSIGN_OR_RETURN(image_index_32, ReadLeInt32(file_.Get()));
      int32_t data_offset_32;
      AIFOCORE_ASSIGN_OR_RETURN(data_offset_32, ReadLeInt32(file_.Get()));
      int32_t data_length_32;
      AIFOCORE_ASSIGN_OR_RETURN(data_length_32, ReadLeInt32(file_.Get()));
      int32_t data_file_number_32;
      AIFOCORE_ASSIGN_OR_RETURN(data_file_number_32, ReadLeInt32(file_.Get()));

      records.push_back(RawIndexRecord{
          /*image_index=*/static_cast<int64_t>(image_index_32),
          /*data_offset=*/static_cast<int64_t>(data_offset_32),
          /*data_length=*/static_cast<int64_t>(data_length_32),
          /*data_file_number=*/static_cast<int64_t>(data_file_number_32)});
    }

    if (next_page_pointer == 0) {
      break;
    }
    AIFOCORE_RETURN_IF_ERROR(file_.Seek(next_page_pointer));
  }

  return records;
}

aifocore::Result<std::vector<MiraxTileRecord>> MrxsIndexReader::ReadLevelTiles(
    int level_index, const PyramidLevelParameters& level_params) {
  const int zoom_levels = static_cast<int>(slide_info_->zoom_levels.size());
  if (level_index < 0 || level_index >= zoom_levels) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level index: {}", level_index));
  }

  const int total_images_horizontal = slide_info_->images_x;
  const SlideZoomLevel& zoom_level = slide_info_->zoom_levels[level_index];

  // The MRXS hierarchical root is a flat array of int32 pointer slots,
  // grouped by hierarchy. Each hierarchy declares one or more
  // *sub-pyramids*: HIER_0 (slide-zoom) has one, HIER_<filter> has
  // `HIER_<filter>_COUNT` (one per declared FilterLevel), and the
  // mask/focus hierarchies each contribute a single sub-pyramid as
  // well. Every sub-pyramid takes `pyramid_depth` slots laid out as
  // `LEVEL_0..LEVEL_{depth-1}`. The slot for HIER_<filter>_VAL_<k> at
  // pyramid level `d` therefore lives at:
  //
  //   slot = (filter_hier_index + k) * pyramid_depth + d
  //
  // which matches what we observe on real 4-channel (s5-cf) and
  // 7-channel (Lung CA Panel) 3DHISTECH slides where every
  // FilterLevel's RGB tile pyramid lives in its own sub-pyramid.
  const int pyramid_depth = static_cast<int>(slide_info_->zoom_levels.size());

  // Read the slide-zoom hierarchy's records for this pyramid level. This
  // is the canonical pixel data: brightfield slides only ever read this,
  // and fluorescence slides treat it as the master/RGB-group-0 source.
  std::vector<RawIndexRecord> primary;
  AIFOCORE_ASSIGN_OR_RETURN(
      primary, ReadLevelRecords(hierarchical_root_ + (4 * level_index)));

  // For multi-channel fluorescence slides (more than 3 channels), the
  // additional RGB tile groups live in the `Slide filter level`
  // hierarchy. Each declared FilterLevel gets its own sub-pyramid; we
  // walk all `HIER_<filter>_COUNT` of them at the requested pyramid
  // level and collect the non-empty ones. Empty/unused sub-pyramids
  // (LayerNN_LEVEL_kk pointing at `page_ptr == 0`) are silently
  // skipped, so 7-channel slides like `Lung CA Panel` (which only
  // populates 3 of 7 declared FilterLevels) still pick up exactly
  // 3 channel groups.
  std::vector<std::vector<RawIndexRecord>> extras;

  if (slide_info_->filter_hier_index > 0 &&
      slide_info_->filter_hier_index <
          static_cast<int>(slide_info_->hier_counts.size())) {
    const int filter_hier = slide_info_->filter_hier_index;
    const int n_filter_sub_pyramids = slide_info_->hier_counts[filter_hier];
    for (int k = 0; k < n_filter_sub_pyramids; ++k) {
      const int64_t pointer_slot =
          static_cast<int64_t>(filter_hier + k) * pyramid_depth + level_index;
      auto rec_or = ReadLevelRecords(hierarchical_root_ + (4 * pointer_slot));
      const size_t n_records = rec_or.ok() ? rec_or->size() : 0;
      int file_no = -1;
      if (rec_or.ok() && !rec_or->empty()) {
        file_no = static_cast<int>(rec_or->front().data_file_number);
      }
      if (rec_or.ok() && !rec_or->empty()) {
        extras.push_back(std::move(*rec_or));
      }
    }
  }

  // Group every record (primary + extras) by `image_index`. The
  // `channel_group_index` we emit is the position in the *retained* slice
  // of that group (after applying the heuristic below).
  struct GroupedEntry {
    RawIndexRecord record;
    size_t source;  ///< 0 = primary, 1+ = extras index + 1
  };

  std::unordered_map<int64_t, std::vector<GroupedEntry>> by_image;
  by_image.reserve(primary.size());
  for (const auto& r : primary) {
    by_image[r.image_index].push_back({r, 0});
  }
  for (size_t e = 0; e < extras.size(); ++e) {
    for (const auto& r : extras[e]) {
      by_image[r.image_index].push_back({r, e + 1});
    }
  }

  // `expected` = the number of channel-group RGB tiles needed to cover all
  // declared filters (3 fluorophores per stored RGB tile). For brightfield
  // slides this is 1.
  const size_t channel_count = std::max<size_t>(slide_info_->filters.size(), 1);
  const int expected_groups =
      static_cast<int>(std::ceil(static_cast<double>(channel_count) / 3.0));

  // Diagnostic counters: how many tiles we emit per channel_group_index,
  // and how many spatial positions had each group size.
  std::unordered_map<int32_t, size_t> tiles_per_group;
  std::unordered_map<int, size_t> spatial_group_sizes;

  std::vector<MiraxTileRecord> tiles;
  tiles.reserve(primary.size());

  // Walk the primary records in their original insertion order so output
  // tiles for a given pyramid level keep their original spatial ordering.
  for (const auto& primary_record : primary) {
    auto it = by_image.find(primary_record.image_index);
    if (it == by_image.end()) {
      continue;
    }
    auto& group = it->second;
    if (group.empty()) {
      continue;
    }

    const int total = static_cast<int>(group.size());
    const int skip = (total > expected_groups) ? (total - expected_groups) : 0;
    spatial_group_sizes[total]++;

    for (int c = 0; c < expected_groups; ++c) {
      const RawIndexRecord* picked = nullptr;
      if (c == 0) {
        // Always emit the HIER_0 (primary) record as group 0 -- mirrors
        // MiraxReader's `return channelOffsets.get(0)` for `c == 0`.
        for (const auto& entry : group) {
          if (entry.source == 0 &&
              entry.record.data_offset == primary_record.data_offset) {
            picked = &entry.record;
            break;
          }
        }
        // Fallback: if for some reason the primary entry was reordered out,
        // pick the first entry.
        if (picked == nullptr) {
          picked = &group.front().record;
        }
      } else {
        const int idx = c + skip;
        if (idx >= total) {
          break;
        }
        picked = &group[idx].record;
      }

      if (picked == nullptr) {
        continue;
      }

      // Skip clearly-bogus records (e.g. 1-byte mask flag entries that
      // accidentally make it through). 3DHISTECH stores per-tile masks as
      // tiny PNGs that should not be decoded as image tiles, so guard
      // against absurdly small payloads.
      if (picked->data_offset < 0 || picked->data_length <= 0) {
        continue;
      }
      if (picked->data_length > constants::kMaxTileSize) {
        return aifocore::Status(
            aifocore::StatusCode::kInvalidArgument,
            aifocore::fmt::format(
                "Data length {} exceeds maximum allowed size of {} for image "
                "index {} at level {}",
                picked->data_length, constants::kMaxTileSize,
                picked->image_index, level_index));
      }
      const int64_t end_offset = picked->data_offset + picked->data_length;
      if (end_offset < 0 ||
          end_offset > std::numeric_limits<int64_t>::max() - 1024) {
        return aifocore::Status(
            aifocore::StatusCode::kInvalidArgument,
            aifocore::fmt::format(
                "Data offset {} + length {} causes overflow for image index "
                "{} at level {}",
                picked->data_offset, picked->data_length, picked->image_index,
                level_index));
      }

      const int32_t image_grid_x =
          static_cast<int32_t>(picked->image_index % total_images_horizontal);
      const int32_t image_grid_y =
          static_cast<int32_t>(picked->image_index / total_images_horizontal);

      auto subtiles = SubdivideImage(
          picked->image_index, image_grid_x, image_grid_y, picked->data_offset,
          picked->data_length, picked->data_file_number,
          /*channel_group_index=*/c, level_index, level_params, zoom_level);
      tiles_per_group[c] += subtiles.size();
      tiles.insert(tiles.end(), subtiles.begin(), subtiles.end());
    }

    // Mark the spatial group as consumed so we don't re-emit if the same
    // image_index appears more than once in the primary record stream
    // (legacy "duplicate image_index = next channel group" layout).
    group.clear();
  }

  return tiles;
}

std::vector<MiraxTileRecord> MrxsIndexReader::SubdivideImage(
    int64_t image_index, int32_t image_grid_x, int32_t image_grid_y,
    int64_t data_offset, int64_t data_length, int64_t data_file_number,
    int32_t channel_group_index, int level_index,
    const PyramidLevelParameters& level_params,
    const SlideZoomLevel& zoom_level) {

  std::vector<MiraxTileRecord> tiles;

  const int total_images_horizontal = slide_info_->images_x;
  const int total_images_vertical = slide_info_->images_y;
  const int camera_image_divisions = slide_info_->image_divisions;

  const double sub_tile_width = static_cast<double>(zoom_level.image_width) /
                                level_params.subtiles_per_stored_image;
  const double sub_tile_height = static_cast<double>(zoom_level.image_height) /
                                 level_params.subtiles_per_stored_image;

  for (int sub_tile_y_idx = 0;
       sub_tile_y_idx < level_params.subtiles_per_stored_image;
       sub_tile_y_idx++) {
    const int tile_grid_y =
        image_grid_y + (sub_tile_y_idx * camera_image_divisions);
    if (tile_grid_y >= total_images_vertical) {
      break;
    }

    for (int sub_tile_x_idx = 0;
         sub_tile_x_idx < level_params.subtiles_per_stored_image;
         sub_tile_x_idx++) {
      const int tile_grid_x =
          image_grid_x + (sub_tile_x_idx * camera_image_divisions);
      if (tile_grid_x >= total_images_horizontal) {
        break;
      }

      MiraxTileRecord tile;
      tile.image_index = static_cast<int32_t>(image_index);
      tile.offset = static_cast<int32_t>(data_offset);
      tile.length = static_cast<int32_t>(data_length);
      tile.data_file_number = static_cast<int32_t>(data_file_number);
      tile.x = tile_grid_x;
      tile.y = tile_grid_y;
      tile.subregion_x = sub_tile_width * sub_tile_x_idx;
      tile.subregion_y = sub_tile_height * sub_tile_y_idx;
      tile.channel_group_index = channel_group_index;

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

aifocore::Result<NonHierRecordData> MrxsIndexReader::ReadNonHierRecord(
    int record_index) {
  // `nonhier_root_` is the file offset of the start of the non-hierarchical
  // record-pointer array (see ReadHeader). Each entry is a 32-bit pointer
  // to that record's header.
  const int64_t record_pointer_offset = nonhier_root_ + 4 * record_index;
  AIFOCORE_RETURN_IF_ERROR(file_.Seek(record_pointer_offset));

  // Read pointer to record header
  int32_t record_header_pointer_32;
  AIFOCORE_ASSIGN_OR_RETURN(record_header_pointer_32, ReadLeInt32(file_.Get()));
  int64_t record_header_pointer = record_header_pointer_32;

  // Navigate to record header
  AIFOCORE_RETURN_IF_ERROR(file_.Seek(record_header_pointer));

  // Read sentinel value (should be 0)
  int32_t sentinel_value;
  AIFOCORE_ASSIGN_OR_RETURN(sentinel_value, ReadLeInt32(file_.Get()));

  if (sentinel_value != 0) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Expected sentinel value 0 at beginning of non-hierarchical "
            "record, got {}",
            sentinel_value));
  }

  // Read pointer to data page
  int32_t data_page_pointer_32;
  AIFOCORE_ASSIGN_OR_RETURN(data_page_pointer_32, ReadLeInt32(file_.Get()));
  int64_t data_page_pointer = data_page_pointer_32;

  // Navigate to data page
  AIFOCORE_RETURN_IF_ERROR(file_.Seek(data_page_pointer));

  // Read page length
  int32_t page_length_32;
  AIFOCORE_ASSIGN_OR_RETURN(page_length_32, ReadLeInt32(file_.Get()));
  int64_t page_length = page_length_32;

  if (page_length < 1) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        "Expected at least one item in non-hierarchical data page");
  }

  // Skip metadata: next page pointer + two reserved fields
  for (int i = 0; i < 3; ++i) {
    int32_t skip;
    AIFOCORE_ASSIGN_OR_RETURN(skip, ReadLeInt32(file_.Get()));
    (void)skip;  // Intentionally unused
  }

  // Read actual data location
  int32_t data_offset_32;
  AIFOCORE_ASSIGN_OR_RETURN(data_offset_32, ReadLeInt32(file_.Get()));
  int64_t data_offset = data_offset_32;

  int32_t data_size_32;
  AIFOCORE_ASSIGN_OR_RETURN(data_size_32, ReadLeInt32(file_.Get()));
  int64_t data_size = data_size_32;

  int32_t datafile_number_32;
  AIFOCORE_ASSIGN_OR_RETURN(datafile_number_32, ReadLeInt32(file_.Get()));
  int64_t datafile_number = datafile_number_32;

  // Validate datafile number
  if (datafile_number < 0 ||
      datafile_number >= static_cast<int>(slide_info_->datafile_paths.size())) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid datafile number: {} (must be 0-{})",
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
