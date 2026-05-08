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

/// @file mrxs.cpp
/// @brief Implementation of MRXS (3DHISTECH/MIRAX) whole slide image reader
///
/// FORMAT SPECIFICATION SOURCES:
/// - OpenSlide format documentation: https://openslide.org/formats/mirax/
/// - Benjamin Gilbert's mailing list explanation (2012-07-24):
///   https://lists.andrew.cmu.edu/pipermail/openslide-users/2012-July/000373.html
/// - Python reference implementation (MIT):
/// https://github.com/rharkes/miraxreader
///
///
/// ## MRXS File Structure
///
/// An MRXS slide consists of:
/// - `<slide>.mrxs`: Main file (minimal content)
/// - `<slide>/Slidedat.ini`: Metadata and configuration (INI format)
/// - `<slide>/Index.dat`: Binary index mapping tiles to data files
/// - `<slide>/Dat_*.dat`: Compressed image data and non-hierarchical records
///
/// ## Key Concepts
///
/// **Tiled Layers (Hierarchical)**: Multi-resolution pyramid with progressive
/// downsampling. Each level concatenates multiple downsampled images from the
/// previous level. These are the main slide image layers used for viewing.
/// Note that due to the overlapping nature of the tiles, the combined image
/// is not an image suitable to present.
///
/// **Non-Tiled Layers (Non-Hierarchical)**: Associated data like labels,
/// macros, thumbnails, and position maps.
///
/// **Camera Positions**: The scanner captures overlapping photos at discrete
/// positions. Position data is stored in non-hierarchical records and MUST be
/// read during initialization for accurate tile positioning.
///
/// **Image Divisions**: Each camera photo may be divided into multiple
/// sub-images (image_divisions^2), which are stored and indexed separately.
///
/// **Tile Subdivision**: At lower zoom levels, stored images may contain
/// multiple logical tiles (subtiles_per_stored_image^2) that need to be
/// extracted as sub-regions.
///
/// **Overlaps**: Adjacent camera positions overlap slightly. Overlap amounts
/// are specified in pixels and must be accounted for when positioning tiles.

#include "fastslide/readers/mrxs/mrxs.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/mrxs/mrxs_constants.h"
#include "fastslide/readers/mrxs/mrxs_data_reader.h"
#include "fastslide/readers/mrxs/mrxs_decoder.h"
#include "fastslide/readers/mrxs/mrxs_exec_context.h"
#include "fastslide/readers/mrxs/mrxs_index_reader.h"
#include "fastslide/readers/mrxs/mrxs_ini_parser.h"
#include "fastslide/readers/mrxs/mrxs_layer_parser.h"
#include "fastslide/readers/mrxs/mrxs_plan_builder.h"
#include "fastslide/readers/mrxs/mrxs_plan_context.h"
#include "fastslide/readers/mrxs/mrxs_tile_executor.h"
#include "fastslide/readers/mrxs/spatial_index.h"
#include "fastslide/runtime/io/binary_utils.h"
#include "fastslide/runtime/io/file_reader.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/hash.h"

namespace fastslide {

namespace {

// Constants for MRXS format
constexpr std::string_view kMrxsExt = ".mrxs";
constexpr std::string_view kSlidedatIni = "Slidedat.ini";

// INI file keys for sections handled directly in this translation unit.
// (Hierarchical / non-hierarchical layer keys live in mrxs_layer_parser.cpp.)
constexpr std::string_view kGroupGeneral = "GENERAL";
constexpr std::string_view kKeySlideId = "SLIDE_ID";
constexpr std::string_view kKeyImageNumberX = "IMAGENUMBER_X";
constexpr std::string_view kKeyImageNumberY = "IMAGENUMBER_Y";
constexpr std::string_view kKeyObjectiveMagnification =
    "OBJECTIVE_MAGNIFICATION";
constexpr std::string_view kKeyCameraImageDivisionsPerSide =
    "CameraImageDivisionsPerSide";
constexpr std::string_view kKeySlideType = "SLIDE_TYPE";
constexpr std::string_view kKeySlideBitDepth = "VIMSLIDE_SLIDE_BITDEPTH";
constexpr std::string_view kSlideTypeFluorescence = "SLIDE_TYPE_FLUORESCENCE";

constexpr std::string_view kGroupDatafile = "DATAFILE";
constexpr std::string_view kKeyFileCount = "FILE_COUNT";
constexpr std::string_view kKeyDataFile = "FILE_{}";

void LogGainMetadataSizeMismatchOnce(size_t expected_size, size_t actual_size) {
  static std::once_flag once_flag;
  std::call_once(once_flag, [expected_size, actual_size]() {
    std::cerr << "MRXS: gain metadata size mismatch (expected " << expected_size
              << " bytes, got " << actual_size
              << "); ignoring gain metadata and using gain=1.0\n";
  });
}

}  // namespace

/// @brief Private constructor for MrxsReader
///
/// Constructs an MrxsReader instance with parsed slide information. Called by
/// the static Create factory method after successful parsing of Slidedat.ini.
/// Initializes level parameters and spatial indices.
///
/// @param dirname Path to MRXS directory (same as .mrxs filename minus
/// extension)
/// @param slide_info Parsed slide information from Slidedat.ini
MrxsReader::MrxsReader(fs::path dirname, mrxs::SlideDataInfo slide_info)
    : dirname_(std::move(dirname)), slide_info_(slide_info) {
  level_params_ = CalculateLevelParams();
  spatial_indices_.resize(slide_info_.zoom_levels.size());
}

/// @brief Factory method to create an MrxsReader from a file
///
/// Opens and parses an MRXS slide file, creating a fully initialized reader
/// instance. Validates file structure, parses metadata, and optionally loads
/// camera position data.
///
/// @param filename Path to the .mrxs file
/// @return Result containing unique pointer to MrxsReader or error
/// @retval InvalidArgument if file extension is not .mrxs
/// @retval NotFound if file or required data files don't exist
/// @retval Internal if parsing fails
aifocore::Result<std::unique_ptr<MrxsReader>> MrxsReader::Create(
    fs::path filename) {
  return CreateImpl(filename);
}

aifocore::Status MrxsReader::ValidateInput(const fs::path& filename) {
  // Verify filename has .mrxs extension
  if (filename.extension() != kMrxsExt) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("File does not have {} extension", kMrxsExt));
  }

  // Check file exists
  if (!fs::exists(filename)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("File does not exist: {}", filename.string()));
  }

  // Get directory name (remove .mrxs extension)
  fs::path dirname = filename.parent_path() / filename.stem();

  // Check if Slidedat.ini exists
  fs::path slidedat_path = dirname / kSlidedatIni;
  if (!fs::exists(slidedat_path)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("{} does not exist: {}", kSlidedatIni,
                              slidedat_path.string()));
  }

  return aifocore::Status::OkStatus();
}

aifocore::Result<std::unique_ptr<MrxsReader>> MrxsReader::CreateReaderImpl(
    const fs::path& filename) {
  // Get directory name (remove .mrxs extension)
  fs::path dirname = filename.parent_path() / filename.stem();
  fs::path slidedat_path = dirname / kSlidedatIni;

  // Read Slidedat.ini
  mrxs::SlideDataInfo slide_info;
  AIFOCORE_ASSIGN_OR_RETURN(slide_info,
                            ReadSlidedatIni(slidedat_path, dirname));

  // IMPORTANT: Camera positions MUST be read during initialization!
  // They are required for accurate tile positioning when reading image data.
  AIFOCORE_RETURN_IF_ERROR(ReadCameraPositions(dirname, slide_info));

  auto reader = std::unique_ptr<MrxsReader>(
      new MrxsReader(std::move(dirname), std::move(slide_info)));

  AIFOCORE_RETURN_IF_ERROR(reader->InitializeProperties());

  return reader;
}

/// @brief Read and parse the Slidedat.ini metadata file
///
/// Parses the MRXS metadata file containing slide configuration, pyramid
/// structure, and level parameters. Extracts all necessary information for
/// initializing the reader.
///
/// @param slidedat_path Path to Slidedat.ini file
/// @param dirname Path to MRXS directory for cache keys
/// @return Result containing SlideDataInfo or error
/// @retval NotFound if required sections or keys are missing
/// @retval InvalidArgument if values cannot be parsed
aifocore::Result<mrxs::SlideDataInfo> MrxsReader::ReadSlidedatIni(
    const fs::path& slidedat_path, const fs::path& dirname) {
  mrxs::internal::IniFile ini;
  AIFOCORE_ASSIGN_OR_RETURN(ini, mrxs::internal::IniFile::Load(slidedat_path));

  mrxs::SlideDataInfo info;
  info.dirname = dirname.string();  // Store dirname for cache keys

  // Read general section
  if (!ini.HasSection(kGroupGeneral)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Missing section: {}", kGroupGeneral));
  }

  AIFOCORE_ASSIGN_OR_RETURN(info.slide_id,
                            ini.GetString(kGroupGeneral, kKeySlideId));
  AIFOCORE_ASSIGN_OR_RETURN(info.images_x,
                            ini.GetInt(kGroupGeneral, kKeyImageNumberX));
  AIFOCORE_ASSIGN_OR_RETURN(info.images_y,
                            ini.GetInt(kGroupGeneral, kKeyImageNumberY));

  AIFOCORE_ASSIGN_OR_RETURN(
      info.objective_magnification,
      ini.GetInt(kGroupGeneral, kKeyObjectiveMagnification));

  auto image_divisions =
      ini.GetInt(kGroupGeneral, kKeyCameraImageDivisionsPerSide);
  info.image_divisions = image_divisions.ok() ? *image_divisions : 1;

  // Optional: slide modality + camera bit depth.
  auto slide_type_or = ini.GetString(kGroupGeneral, kKeySlideType);
  if (slide_type_or.ok()) {
    info.slide_type_raw = *slide_type_or;
    if (*slide_type_or == kSlideTypeFluorescence) {
      info.slide_type = mrxs::MrxsSlideType::kFluorescence;
    }
  }
  auto bitdepth_or = ini.GetInt(kGroupGeneral, kKeySlideBitDepth);
  if (bitdepth_or.ok()) {
    info.camera_bitdepth = *bitdepth_or;
  }

  // Parse non-tiled (non-hierarchical) layers
  // Note: We must call this before ParseTiledLayers if we need positions to
  // validate tiled layers, but currently it's the other way around? Actually
  // ParseTiledLayers sets up zoom levels which we might need. But for CMU-1
  // failure, it happens inside ParseTiledLayers -> HIER_x_COUNT not found.
  AIFOCORE_RETURN_IF_ERROR(mrxs::internal::ParseNonTiledLayers(ini, info));

  // Parse tiled (hierarchical/zoom) layers
  AIFOCORE_RETURN_IF_ERROR(mrxs::internal::ParseTiledLayers(ini, info));

  // Parse fluorescence filter channels (no-op for brightfield slides).
  AIFOCORE_RETURN_IF_ERROR(mrxs::internal::ParseFilterChannels(ini, info));

  // Read datafile section
  if (!ini.HasSection(kGroupDatafile)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Missing section: {}", kGroupDatafile));
  }

  int file_count;
  AIFOCORE_ASSIGN_OR_RETURN(file_count,
                            ini.GetInt(kGroupDatafile, kKeyFileCount));

  for (int i = 0; i < file_count; ++i) {
    std::string key = aifocore::fmt::format(kKeyDataFile, i);
    std::string file_path;
    AIFOCORE_ASSIGN_OR_RETURN(file_path, ini.GetString(kGroupDatafile, key));
    info.datafile_paths.push_back(file_path);
  }

  return info;
}

/// @brief Initialize slide properties from parsed metadata
///
/// Populates the SlideProperties structure with metadata extracted from
/// Slidedat.ini. Sets microns per pixel (MPP), objective magnification,
/// scanner model, and calculates slide bounds.
aifocore::Status MrxsReader::InitializeProperties() {
  // Set basic properties
  if (!slide_info_.zoom_levels.empty()) {
    const auto& level0 = slide_info_.zoom_levels[0];
    properties_.mpp = {level0.mpp_x, level0.mpp_y};
    properties_.objective_magnification =
        static_cast<double>(slide_info_.objective_magnification);
  }
  properties_.scanner_model = "3DHISTECH";

  // Calculate bounds from level 0 tiles
  AIFOCORE_ASSIGN_OR_RETURN(properties_.bounds, CalculateBounds());

  return aifocore::Status::OkStatus();
}

/// @brief Calculate the bounding box of non-background tissue
///
/// Computes slide bounds in a single pass over tile metadata without
/// constructing the spatial index. This avoids the overhead of hashmap
/// construction and SoA layout building used by the spatial index.
///
/// **Algorithm:**
/// 1. Read level 0 tiles directly from index file via ReadLevelTiles(0)
/// 2. Single pass: compute bounding box for each tile and track extremes inline
/// 3. Calculate final bounds from the extreme coordinates
/// 4. Clamp to level 0 dimensions to ensure valid bounds
///
/// @return Result containing SlideBounds or error
aifocore::Result<SlideBounds> MrxsReader::CalculateBounds() {
  // Read tiles directly (no spatial index needed)
  std::vector<mrxs::MiraxTileRecord> tiles;
  AIFOCORE_ASSIGN_OR_RETURN(tiles, ReadLevelTiles(0));

  if (tiles.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No tiles found at level 0");
  }

  // Get level 0 dimensions for clamping
  LevelInfo level0;
  AIFOCORE_ASSIGN_OR_RETURN(level0, GetLevelInfo(0));
  const int64_t slide_width = level0.dimensions[0];
  const int64_t slide_height = level0.dimensions[1];

  // Get level params for bbox calculation
  const auto& level_params = level_params_[0];

  // Track extreme bounding boxes (not tiles, since we don't store them)
  double leftmost_min_x = std::numeric_limits<double>::max();
  double topmost_min_y = std::numeric_limits<double>::max();
  double rightmost_max_x = std::numeric_limits<double>::lowest();
  double bottommost_max_y = std::numeric_limits<double>::lowest();

  size_t active_tiles = 0;

  // Single-pass: compute bbox and track extremes
  for (const auto& tile : tiles) {
    mrxs::Box bbox = mrxs::MrxsSpatialIndex::CalculateTileBoundingBox(
        tile, level_params, 0, slide_info_);

    // Skip inactive tiles (negative coords)
    if (bbox.min[0] < 0 || bbox.min[1] < 0) {
      continue;
    }

    active_tiles++;

    // Track extremes by comparing only the relevant coordinates
    if (bbox.min[0] < leftmost_min_x) {
      leftmost_min_x = bbox.min[0];
    }
    if (bbox.min[1] < topmost_min_y) {
      topmost_min_y = bbox.min[1];
    }
    if (bbox.max[0] > rightmost_max_x) {
      rightmost_max_x = bbox.max[0];
    }
    if (bbox.max[1] > bottommost_max_y) {
      bottommost_max_y = bbox.max[1];
    }
  }

  // Handle case where no valid tiles were found
  if (active_tiles == 0) {
    return SlideBounds();
  }

  // Calculate bounds from extremes
  int64_t min_x = static_cast<int64_t>(std::floor(leftmost_min_x));
  int64_t min_y = static_cast<int64_t>(std::floor(topmost_min_y));
  int64_t max_x = static_cast<int64_t>(std::ceil(rightmost_max_x));
  int64_t max_y = static_cast<int64_t>(std::ceil(bottommost_max_y));

  // Clamp to slide dimensions to ensure we're always within level 0
  min_x = std::clamp<int64_t>(min_x, 0, slide_width);
  min_y = std::clamp<int64_t>(min_y, 0, slide_height);
  max_x = std::clamp<int64_t>(max_x, 0, slide_width);
  max_y = std::clamp<int64_t>(max_y, 0, slide_height);

  int64_t width = max_x - min_x;
  int64_t height = max_y - min_y;

  // Ensure non-negative dimensions
  width = std::max<int64_t>(0, width);
  height = std::max<int64_t>(0, height);

  return SlideBounds(min_x, min_y, width, height);
}

/// @brief Calculate level parameters for tile subdivision and positioning
///
/// MRXS uses a hierarchical multi-resolution structure where each level is
/// created by:
/// 1. Downsampling images by 2x
/// 2. Concatenating multiple downsampled images into single larger images
///
/// This function computes parameters for:
/// - How many original tiles are concatenated (concatenation_factor = 2^(sum of
/// concat_exponents))
/// - How many logical tiles each stored image should be subdivided into
/// - Tile spacing accounting for overlaps between camera positions
///
/// Key concepts:
/// - concatenation_factor: Accumulates across levels (e.g., 1 -> 2 -> 4 -> 8)
/// - subtiles_per_stored_image: How many tiles each stored image contains
/// - horizontal/vertical_tile_step: Pixel spacing between tile centers
/// (accounting for overlaps)
///
/// @return Vector of parameters, one per zoom level
std::vector<mrxs::PyramidLevelParameters> MrxsReader::CalculateLevelParams()
    const {
  std::vector<mrxs::PyramidLevelParameters> params;

  // Concatenation exponents accumulate across levels
  // Level 0 might have concat_exp=0 (no concat), level 1 concat_exp=2 (4x
  // concat), etc.
  int accumulated_downsample_exponent = 0;

  for (size_t level_idx = 0; level_idx < slide_info_.zoom_levels.size();
       ++level_idx) {
    const auto& zoom_level = slide_info_.zoom_levels[level_idx];
    mrxs::PyramidLevelParameters level_params;

    // Accumulate concatenation exponent: each level adds to previous
    // concatenation_factor = 2^(accumulated_exponent) represents how many base
    // images have been concatenated in each dimension
    accumulated_downsample_exponent += zoom_level.downsample_exponent;
    level_params.concatenation_factor = 1 << accumulated_downsample_exponent;

    // positions_per_image: how many camera positions are in each stored image
    const int positions_per_image = std::max(
        1, level_params.concatenation_factor / slide_info_.image_divisions);

    const bool has_overlaps_or_positions =
        !slide_info_.using_synthetic_positions ||
        slide_info_.zoom_levels[0].x_overlap_pixels != 0.0 ||
        slide_info_.zoom_levels[0].y_overlap_pixels != 0.0;

    if (has_overlaps_or_positions) {
      // Branch 1: slide has position data or overlapping tiles.
      // Subdivide stored images into multiple logical tiles for precise
      // overlap blending and positioning.
      level_params.grid_divisor = std::min(level_params.concatenation_factor,
                                           slide_info_.image_divisions);
      level_params.subtiles_per_stored_image = positions_per_image;
      level_params.camera_positions_per_tile = 1;
    } else {
      // Branch 2: no position data and no overlaps.
      // One tile per stored image; no subdivision needed.
      level_params.grid_divisor = level_params.concatenation_factor;
      level_params.subtiles_per_stored_image = 1;
      level_params.camera_positions_per_tile = positions_per_image;
    }

    // Logical tile dimensions within each stored image
    const double logical_tile_width =
        static_cast<double>(zoom_level.image_width) /
        level_params.subtiles_per_stored_image;
    const double logical_tile_height =
        static_cast<double>(zoom_level.image_height) /
        level_params.subtiles_per_stored_image;

    // Calculate how many stored images come from a single camera position
    // At high resolution: images_per_position > 1 (position split across
    // images) At low resolution: images_per_position = 1 (entire position in
    // one image)
    const int images_per_camera_position = std::max(
        1, slide_info_.image_divisions / level_params.concatenation_factor);

    // Calculate tile advance (center-to-center spacing) accounting for overlaps
    // Overlaps are specified for full camera images, so we divide by
    // images_per_position to get per-tile overlap
    level_params.horizontal_tile_step =
        logical_tile_width - (zoom_level.x_overlap_pixels /
                              static_cast<double>(images_per_camera_position));
    level_params.vertical_tile_step =
        logical_tile_height - (zoom_level.y_overlap_pixels /
                               static_cast<double>(images_per_camera_position));

    params.push_back(level_params);
  }

  return params;
}

/// @brief Get the number of pyramid levels in the slide
///
/// @return Number of resolution levels (0-indexed)
int MrxsReader::GetLevelCount() const {
  return static_cast<int>(slide_info_.zoom_levels.size());
}

/// @brief Get information about a specific pyramid level
///
/// Returns dimensions and downsample factor for the requested level.
/// Level 0 is the highest resolution, with each successive level being
/// a downsampled version.
///
/// @param level Level index (0-based, 0 = highest resolution)
/// @return Result containing LevelInfo or error
/// @retval InvalidArgument if level is out of range
aifocore::Result<LevelInfo> MrxsReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  const auto& params = level_params_[level];

  // Calculate downsample factor
  const auto& level0_params = level_params_[0];
  double downsample_factor =
      static_cast<double>(params.concatenation_factor) /
      static_cast<double>(level0_params.concatenation_factor);

  int64_t base_w = 0;
  int64_t base_h = 0;

  const auto& level0 = slide_info_.zoom_levels[0];
  const int image_divisions = slide_info_.image_divisions;
  const int images_x = slide_info_.images_x;
  const int images_y = slide_info_.images_y;

  for (int i = 0; i < images_x; i++) {
    if (((i % image_divisions) != (image_divisions - 1)) ||
        (i == images_x - 1)) {
      // Full size
      base_w += level0.image_width;
    } else {
      // Size minus overlap
      base_w += level0.image_width -
                static_cast<int64_t>(std::ceil(level0.x_overlap_pixels));
    }
  }

  for (int i = 0; i < images_y; i++) {
    if (((i % image_divisions) != (image_divisions - 1)) ||
        (i == images_y - 1)) {
      // Full size
      base_h += level0.image_height;
    } else {
      base_h += level0.image_height -
                static_cast<int64_t>(std::ceil(level0.y_overlap_pixels));
    }
  }

  // Calculate this level's dimensions from base
  uint32_t level_width =
      static_cast<uint32_t>(base_w / params.concatenation_factor);
  uint32_t level_height =
      static_cast<uint32_t>(base_h / params.concatenation_factor);

  LevelInfo info;
  info.dimensions = ImageDimensions{level_width, level_height};
  info.downsample_factor = downsample_factor;

  return info;
}

/// @brief Get slide properties (MPP, magnification, bounds, etc.)
///
/// @return Reference to SlideProperties structure
const SlideProperties& MrxsReader::GetProperties() const {
  return properties_;
}

/// @brief Get metadata for each color channel
///
/// For brightfield slides, MRXS tiles are RGB and we return three channels
/// labelled Red/Green/Blue. For fluorescence slides, each plane of the RGB
/// tile holds a different fluorophore; the per-channel metadata then comes
/// from the parsed `FilterChannel` entries (sorted by `storing_channel`).
///
/// @return Vector of ChannelMetadata, one per channel
std::vector<ChannelMetadata> MrxsReader::GetChannelMetadata() const {
  const std::string bit_depth_str = std::to_string(slide_info_.camera_bitdepth);

  if (slide_info_.slide_type == mrxs::MrxsSlideType::kFluorescence &&
      !slide_info_.filters.empty()) {
    // Fluorescence slides pack up to 3 filters into a stored RGB tile via
    // STORING_CHANNEL_NUMBER (R=0, G=1, B=2). Slides with more than 3
    // filters use multiple FilterLevel groups (DATA_IN_THIS_FILTER_LEVEL
    // = "FilterLevel_0", "FilterLevel_1", ...) where each level adds another
    // stored RGB tile. Sort by (data_filter_level, storing_channel) so the
    // public channel order matches the on-disk channel-group layout used by
    // the plan builder / tile executor.
    std::vector<mrxs::FilterChannel> sorted = slide_info_.filters;
    std::sort(sorted.begin(), sorted.end(),
              [](const mrxs::FilterChannel& a, const mrxs::FilterChannel& b) {
                if (a.data_filter_level != b.data_filter_level) {
                  return a.data_filter_level < b.data_filter_level;
                }
                return a.storing_channel < b.storing_channel;
              });

    std::vector<ChannelMetadata> channels;
    channels.reserve(sorted.size());
    for (const auto& filter : sorted) {
      ChannelMetadata ch;
      ch.name = filter.name;
      ch.color = ColorRGB{filter.color_rgb[0], filter.color_rgb[1],
                          filter.color_rgb[2]};
      ch.exposure_time =
          static_cast<uint32_t>(std::max(0, filter.exposure_time_us));
      ch.additional["storing_channel"] = std::to_string(filter.storing_channel);
      ch.additional["excitation_wavelength_nm"] =
          aifocore::fmt::format("{}", filter.excitation_wavelength);
      ch.additional["emission_wavelength_nm"] =
          aifocore::fmt::format("{}", filter.emission_wavelength);
      ch.additional["digital_gain"] = std::to_string(filter.digital_gain);
      ch.additional["data_filter_level"] = filter.data_filter_level;
      ch.additional["is_master"] = filter.is_master ? "true" : "false";
      ch.additional["is_stitching"] = filter.is_stitching ? "true" : "false";
      ch.additional["bit_depth"] = bit_depth_str;
      channels.push_back(std::move(ch));
    }
    return channels;
  }

  ChannelMetadata red, green, blue;
  red.name = "Red";
  red.color = ColorRGB{255, 0, 0};
  red.additional["bit_depth"] = bit_depth_str;

  green.name = "Green";
  green.color = ColorRGB{0, 255, 0};
  green.additional["bit_depth"] = bit_depth_str;

  blue.name = "Blue";
  blue.color = ColorRGB{0, 0, 255};
  blue.additional["bit_depth"] = bit_depth_str;

  return {red, green, blue};
}

/// @brief Get names of associated images (label, macro, thumbnail)
///
/// MRXS format stores associated images in non-hierarchical records rather
/// than as traditional associated images. Use GetAssociatedDataNames() instead
/// @brief Names of MRXS non-hierarchical layers exposed as associated images.
///
/// MRXS stores auxiliary images (macro, label, …) inside non-hierarchical
/// `ScanDataLayer_Slide*` records. We surface them as standard
/// `SlideReader` associated images by stripping the
/// `ScanDataLayer_Slide` prefix, so callers can use the regular
/// `GetAssociatedImageNames` / `ReadAssociatedImage` API uniformly across
/// formats.
std::vector<std::string> MrxsReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  for (const auto& full_name : GetAssociatedDataNames()) {
    if (full_name.find(mrxs::internal::kAssociatedImagePrefix) == 0) {
      names.emplace_back(
          full_name.substr(mrxs::internal::kAssociatedImagePrefix.size()));
    }
  }
  return names;
}

/// @brief Get dimensions of an MRXS associated image.
///
/// Re-adds the `ScanDataLayer_Slide` prefix to @p name and looks the entry
/// up via the non-hierarchical layer machinery.
aifocore::Result<ImageDimensions> MrxsReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  AIFOCORE_ASSIGN_OR_RETURN(auto image, ReadAssociatedImage(name));
  return image.GetDimensions();
}

/// @brief Read an MRXS associated image.
///
/// Re-adds the `ScanDataLayer_Slide` prefix to @p name and decodes the
/// matching non-hierarchical record.
aifocore::Result<RGBImage> MrxsReader::ReadAssociatedImage(
    std::string_view name) const {
  std::string full_name =
      std::string(mrxs::internal::kAssociatedImagePrefix) + std::string(name);
  AIFOCORE_ASSIGN_OR_RETURN(auto data, LoadAssociatedData(full_name));
  if (!data.IsImage()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image not found: {}", name));
  }
  const auto* image = data.GetImage();
  if (image == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Associated image '{}' decoded to null payload",
                              name));
  }
  return std::move(*image->Clone());
}

/// @brief Get slide metadata as key-value pairs
///
/// Returns metadata extracted from Slidedat.ini including format information,
/// physical parameters (MPP), magnification, and scanner details. All keys
/// use the standard MetadataKeys constants.
///
/// @return Metadata map with slide information
Metadata MrxsReader::GetMetadata() const {
  Metadata meta;

  // Mandatory keys
  meta[std::string(MetadataKeys::kFormat)] = std::string("MRXS");
  meta[std::string(MetadataKeys::kLevels)] = slide_info_.zoom_levels.size();

  // Optional keys
  if (!slide_info_.zoom_levels.empty()) {
    const auto& level0 = slide_info_.zoom_levels[0];
    meta[std::string(MetadataKeys::kMppX)] = level0.mpp_x;
    meta[std::string(MetadataKeys::kMppY)] = level0.mpp_y;
  }

  meta[std::string(MetadataKeys::kMagnification)] =
      static_cast<double>(slide_info_.objective_magnification);
  meta[std::string(MetadataKeys::kScannerModel)] = std::string("3DHISTECH");
  meta[std::string(MetadataKeys::kSlideID)] = slide_info_.slide_id;
  const size_t num_channels =
      (slide_info_.slide_type == mrxs::MrxsSlideType::kFluorescence &&
       !slide_info_.filters.empty())
          ? slide_info_.filters.size()
          : static_cast<size_t>(3);
  meta[std::string(MetadataKeys::kChannels)] = num_channels;
  meta["bit_depth"] = static_cast<size_t>(slide_info_.camera_bitdepth);
  if (!slide_info_.slide_type_raw.empty()) {
    meta["slide_type"] = slide_info_.slide_type_raw;
  }

  return meta;
}

/// @brief Get the native tile size from level 0
///
/// Returns the dimensions of tiles at the highest resolution level. For MRXS,
/// this corresponds to the digitizer width and height.
///
/// @return ImageDimensions of native tiles, or {0,0} if no levels exist
ImageDimensions MrxsReader::GetTileSize() const {
  if (slide_info_.zoom_levels.empty()) {
    return {0, 0};
  }
  const auto& level0 = slide_info_.zoom_levels[0];
  return {static_cast<uint32_t>(level0.image_width),
          static_cast<uint32_t>(level0.image_height)};
}

/// @brief Generate a quick hash for slide identification
///
/// Computes an OpenSlide-compatible hash by hashing:
/// 1. The entire Slidedat.ini file
/// 2. All raw compressed tile data (JPEG/PNG/BMP) from the lowest resolution
/// level
///
/// Note: This hashes the raw compressed data bytes, NOT the decoded pixel data.
/// This ensures compatibility with OpenSlide's MIRAX quickhash implementation.
///
/// This provides a fast fingerprint for slide identification without decoding
/// all data. Compatible with OpenSlide's MIRAX quickhash.
///
/// @return Result containing hexadecimal hash string or error
aifocore::Result<std::string> MrxsReader::GetQuickHash() const {
  // OpenSlide-compatible quickhash: hash Slidedat.ini + all lowest-res tile
  // data
  QuickHashBuilder hasher;

  // Hash the Slidedat.ini file
  fs::path slidedat_path = dirname_ / kSlidedatIni;
  AIFOCORE_RETURN_IF_ERROR(hasher.HashFile(slidedat_path));

  // Hash all tiles from the lowest resolution level (highest level index)
  const int lowest_res_level = GetLevelCount() - 1;
  if (lowest_res_level < 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No pyramid levels available");
  }

  std::vector<mrxs::MiraxTileRecord> tiles_or;
  AIFOCORE_ASSIGN_OR_RETURN(tiles_or, ReadLevelTiles(lowest_res_level));

  // Hash each UNIQUE image's RAW COMPRESSED data (not decoded) - must match
  // OpenSlide Important: When subtiles_per_stored_image > 1, multiple tiles
  // share the same source image. OpenSlide hashes each unique image only once,
  // so we must deduplicate by (file_number, offset) to match.
  std::set<std::pair<int32_t, int32_t>> hashed_images;  // (file_number, offset)

  for (const auto& tile : tiles_or) {
    // Skip if we've already hashed this image
    auto image_key = std::make_pair(tile.data_file_number, tile.offset);
    if (hashed_images.count(image_key) > 0) {
      continue;  // Already hashed this unique image
    }

    // Validate file number
    if (tile.data_file_number < 0 ||
        tile.data_file_number >=
            static_cast<int>(slide_info_.datafile_paths.size())) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "Invalid data file number {} for tile at ({}, {})",
              tile.data_file_number, tile.x, tile.y));
    }

    // Hash the raw compressed data directly from the data file
    fs::path data_file_path =
        dirname_ / slide_info_.datafile_paths[tile.data_file_number];
    AIFOCORE_RETURN_IF_ERROR(
        hasher.HashFilePart(data_file_path, tile.offset, tile.length));

    // Mark this image as hashed
    hashed_images.insert(image_key);
  }

  return hasher.Finalize();
}

/// @brief Read a region with fractional pixel coordinates (MRXS-specific)
///
/// Extended version of ReadRegion that accepts fractional x/y coordinates for
/// precise sub-pixel positioning. Useful for handling overlapping tiles where
/// exact positioning matters.
///
/// This now delegates to the two-stage pipeline which uses Canvas
/// with Magic Kernel resampling for subpixel-accurate positioning.
///
/// @param level Pyramid level (0 = highest resolution)
/// @param x X coordinate (fractional, in level-native space)
/// @param y Y coordinate (fractional, in level-native space)
/// @param width Width in pixels
/// @param height Height in pixels
/// @return Result containing RGB image or error
/// @retval InvalidArgument if level is invalid
aifocore::Result<RGBImage> MrxsReader::ReadRegionFractional(
    int level, double x, double y, uint32_t width, uint32_t height) const {
  // Validate level
  // TODO(jonasteuwen): Check how different this is and if it can't be merged
  // into the CRTP pattern
  if (level < 0 || level >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  // Create TileRequest directly with fractional region bounds
  // This preserves the full double precision through the pipeline
  core::TileRequest request;
  request.level = level;
  request.tile_coord = {0, 0};  // Not meaningful for region requests
  request.channel_indices = visible_channels_;

  // Populate fractional bounds (preserves fractional coordinates!)
  core::FractionalRegionBounds bounds;
  bounds.x = x;  // Keep full double precision
  bounds.y = y;
  bounds.width = static_cast<double>(width);
  bounds.height = static_cast<double>(height);
  request.region_bounds = bounds;

  core::TilePlan plan;
  AIFOCORE_ASSIGN_OR_RETURN(plan, PrepareRequest(request));

  // Use TilePlan-based Canvas so 16-bit fluorescence slides flow through the
  // generic UInt16 paint path instead of the RGB8 blender.
  runtime::Canvas writer(plan);

  // Execute the plan
  AIFOCORE_RETURN_IF_ERROR(ExecutePlan(plan, writer));
  AIFOCORE_RETURN_IF_ERROR(writer.Finalize());

  Image image;
  AIFOCORE_ASSIGN_OR_RETURN(image, writer.GetOutput());

  // Wrap the output image (which may be UInt8 or UInt16) into an RGBImage
  // alias. RGBImage is just `using Image`, so this preserves the data type.
  return image;
}

/// @brief Get or build the spatial index for a pyramid level
///
/// Returns a cached spatial index for fast tile queries, building it on first
/// access. The index uses an R-tree structure to efficiently find tiles
/// intersecting arbitrary regions.
///
/// Thread-safe: uses mutex to protect lazy initialization.
///
/// @param level Pyramid level (0-based)
/// @return Result containing shared pointer to spatial index or error
/// @retval InvalidArgument if level is out of range
/// @note First call for a level performs I/O to read index file
aifocore::Result<std::shared_ptr<mrxs::MrxsSpatialIndex>>
MrxsReader::GetSpatialIndex(int level) const {
  std::lock_guard<std::mutex> lock(spatial_index_mutex_);

  if (spatial_indices_[level]) {
    return spatial_indices_[level];
  }

  // Build spatial index
  std::vector<mrxs::MiraxTileRecord> tiles;
  AIFOCORE_ASSIGN_OR_RETURN(tiles, ReadLevelTiles(level));

  std::unique_ptr<mrxs::MrxsSpatialIndex> index;
  AIFOCORE_ASSIGN_OR_RETURN(
      index, mrxs::MrxsSpatialIndex::Build(tiles, level_params_[level], level,
                                           slide_info_));

  spatial_indices_[level] = std::move(index);
  return spatial_indices_[level];
}

/// @brief Read all tile metadata for a specific zoom level from the index file
///
/// Delegates to MrxsIndexReader helper class for index file parsing.
/// This method now simply opens the index reader and calls its ReadLevelTiles
/// method, reducing complexity in the main reader class.
///
/// @param level_index Zero-based level index (0 = highest resolution)
/// @return Vector of tile metadata or error status
aifocore::Result<std::vector<mrxs::MiraxTileRecord>> MrxsReader::ReadLevelTiles(
    int level_index) const {
  if (level_index < 0 || level_index >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level index: {}", level_index));
  }

  // Open index file using helper class
  fs::path index_path = dirname_ / slide_info_.index_filename;
  mrxs::MrxsIndexReader index_reader;
  AIFOCORE_ASSIGN_OR_RETURN(
      index_reader, mrxs::MrxsIndexReader::Open(index_path, slide_info_));

  // Delegate to index reader
  const auto& level_params = level_params_[level_index];
  return index_reader.ReadLevelTiles(level_index, level_params);
}

/// @brief Read a non-hierarchical record from the index file
///
/// Delegates to MrxsIndexReader helper class for index file parsing.
/// A simple wrapper that opens the index reader and calls its
/// ReadNonHierRecord method.
///
/// @param record_index Zero-based index of the record to read
/// @return Tuple of (datafile_path, data_offset, data_size) or error status
aifocore::Result<std::tuple<std::string, int64_t, int64_t>>
MrxsReader::ReadNonHierRecord(int record_index) const {
  // Open index file using helper class
  fs::path index_path = dirname_ / slide_info_.index_filename;
  mrxs::MrxsIndexReader index_reader;
  AIFOCORE_ASSIGN_OR_RETURN(
      index_reader, mrxs::MrxsIndexReader::Open(index_path, slide_info_));

  // Delegate to index reader
  mrxs::NonHierRecordData record_data;
  AIFOCORE_ASSIGN_OR_RETURN(record_data,
                            index_reader.ReadNonHierRecord(record_index));

  return std::make_tuple(record_data.datafile_path, record_data.offset,
                         record_data.size);
}

/// @brief Read actual camera positions from slide position file
///
/// FORMAT SPECIFICATION (from OpenSlide documentation):
/// https://lists.andrew.cmu.edu/pipermail/openslide-users/2012-July/000373.html
/// (step 11)
///
/// Position buffer structure (per camera position, 9 bytes each):
///   - flag: uint8 (typically 0 or 1, meaning unknown)
///   - x: int32 (little-endian, level 0 pixel coordinate)
///   - y: int32 (little-endian, level 0 pixel coordinate)
///
/// Total size = 9 * (images_x / image_divisions) * (images_y / image_divisions)
///
/// Storage locations (non-hierarchical records):
///   1. VIMSLIDE_POSITION_BUFFER (older slides):
///      - Uncompressed raw position data
///   2. StitchingIntensityLayer (newer slides):
///      - DEFLATE/zlib compressed (magic: 0x78 0x9C)
///
/// Position scaling:
///   - Positions in file are for smallest tile unit
///   - Must multiply by level_0_concat factor to get actual level 0 coordinates
///   - slide_info.camera_positions stores scaled coordinates
///
/// @param dirname Path to MRXS directory
/// @param slide_info Slide information with position layer metadata from INI
/// parsing
/// @return OkStatus on success, error status on failure
/// @retval NotFound if position data file cannot be opened
/// @retval Internal if file operations fail
/// @retval InvalidArgument if data format is invalid
/// @note Handles both uncompressed and DEFLATE-compressed position data
/// @note Uses position layer metadata populated during ParseNonTiledLayers()
/// @note If position data unavailable, using_synthetic_positions is already set
aifocore::Status MrxsReader::ReadCameraPositions(
    const fs::path& dirname, mrxs::SlideDataInfo& slide_info) {
  // Check if position data is available (determined during INI parsing)
  if (slide_info.using_synthetic_positions ||
      slide_info.position_layer_record_offset == -1) {
    // No position data found, synthetic positions already set
    return aifocore::Status::OkStatus();
  }

  // Use pre-populated position layer metadata
  const int position_record = slide_info.position_layer_record_offset;
  const bool is_stitching_layer = slide_info.position_layer_compressed;

  // Open index file using RAII wrapper
  fs::path index_path = dirname / slide_info.index_filename;
  FileReader indexfile;
  AIFOCORE_ASSIGN_OR_RETURN(indexfile, FileReader::Open(index_path, "rb"));

  // Calculate nonhier root
  const int64_t hier_root = strlen("01.02") + slide_info.slide_id.length();

  const int64_t nonhier_root = hier_root + 4;

  // Read the position record inline (avoiding temp reader construction)
  // Seek to nonhier root
  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(nonhier_root));

  // Read pointer
  int32_t ptr_32;
  AIFOCORE_ASSIGN_OR_RETURN(ptr_32, ReadLeInt32(indexfile.Get()));
  int64_t ptr = ptr_32;

  // Seek to record pointer
  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(ptr + 4 * position_record));

  // Read pointer to record data
  int32_t record_ptr_32;
  AIFOCORE_ASSIGN_OR_RETURN(record_ptr_32, ReadLeInt32(indexfile.Get()));
  int64_t record_ptr = record_ptr_32;

  // Seek to record data
  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(record_ptr));

  // Read initial 0
  int32_t zero_value_32;
  AIFOCORE_ASSIGN_OR_RETURN(zero_value_32, ReadLeInt32(indexfile.Get()));
  int64_t zero_value = zero_value_32;

  if (zero_value != 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Expected 0 at beginning of nonhier record, got {}", zero_value));
  }

  // Read data pointer
  int32_t data_ptr_32;
  AIFOCORE_ASSIGN_OR_RETURN(data_ptr_32, ReadLeInt32(indexfile.Get()));
  int64_t data_ptr = data_ptr_32;

  // Seek to data page
  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(data_ptr));

  // Read page length
  int32_t page_len_32;
  AIFOCORE_ASSIGN_OR_RETURN(page_len_32, ReadLeInt32(indexfile.Get()));
  int64_t page_len = page_len_32;

  if (page_len < 1) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Expected at least one data item in position data page");
  }

  // Skip next pointer and two zeros
  for (int i = 0; i < 3; ++i) {
    int32_t skip_32;
    AIFOCORE_ASSIGN_OR_RETURN(skip_32, ReadLeInt32(indexfile.Get()));
    (void)skip_32;  // Intentionally unused
  }

  // Read offset, size, fileno for first data item (position data)
  int32_t offset_32;
  AIFOCORE_ASSIGN_OR_RETURN(offset_32, ReadLeInt32(indexfile.Get()));
  int64_t offset = offset_32;

  int32_t size_32;
  AIFOCORE_ASSIGN_OR_RETURN(size_32, ReadLeInt32(indexfile.Get()));
  int64_t size = size_32;

  int32_t fileno_32;
  AIFOCORE_ASSIGN_OR_RETURN(fileno_32, ReadLeInt32(indexfile.Get()));
  int64_t fileno = fileno_32;

  if (fileno < 0 ||
      fileno >= static_cast<int>(slide_info.datafile_paths.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid datafile number: {} (must be 0-{})",
                              fileno, slide_info.datafile_paths.size() - 1));
  }

  // Construct full path to datafile
  fs::path datafile_path = dirname / slide_info.datafile_paths[fileno];

  // Check for second data item (additional metadata per camera position)
  // MRXS slides with version ≥ 2.2 may have this
  if (page_len >= 2) {
    // Skip reserved fields (2 int32s)
    for (int i = 0; i < 2; ++i) {
      int32_t reserved_32;
      AIFOCORE_ASSIGN_OR_RETURN(reserved_32, ReadLeInt32(indexfile.Get()));
      (void)reserved_32;  // Intentionally unused
    }

    // Read second data item location
    int32_t offset2_32;
    AIFOCORE_ASSIGN_OR_RETURN(offset2_32, ReadLeInt32(indexfile.Get()));
    int64_t offset2 = offset2_32;

    int32_t size2_32;
    AIFOCORE_ASSIGN_OR_RETURN(size2_32, ReadLeInt32(indexfile.Get()));
    int64_t size2 = size2_32;

    int32_t fileno2_32;
    AIFOCORE_ASSIGN_OR_RETURN(fileno2_32, ReadLeInt32(indexfile.Get()));
    int64_t fileno2 = fileno2_32;

    // Validate file number
    if (fileno2 < 0 ||
        fileno2 >= static_cast<int>(slide_info.datafile_paths.size())) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Invalid gain metadata file number: {}",
                                fileno2));
    }

    // Read metadata from data file using helper
    fs::path datafile2_path = dirname / slide_info.datafile_paths[fileno2];
    std::vector<uint8_t> compressed_metadata;
    AIFOCORE_ASSIGN_OR_RETURN(
        compressed_metadata,
        mrxs::MrxsDataReader::ReadData(datafile2_path, offset2, size2));

    // Decompress if needed (zlib header)
    if (compressed_metadata.size() >= 2 && compressed_metadata[0] == 0x78 &&
        compressed_metadata[1] == 0x9C) {
      const int positions_x = slide_info.images_x / slide_info.image_divisions;
      const int positions_y = slide_info.images_y / slide_info.image_divisions;
      const int npositions = positions_x * positions_y;
      const int expected_size = 4 * npositions;

      AIFOCORE_ASSIGN_OR_RETURN(
          runtime::io::ZlibDecompressionResult decompressed,
          DecompressZlibWithActualSize(compressed_metadata.data(),
                                       compressed_metadata.size(),
                                       /*expected_size_hint=*/expected_size));
      if (decompressed.actual_size_bytes !=
          static_cast<size_t>(expected_size)) {
        LogGainMetadataSizeMismatchOnce(static_cast<size_t>(expected_size),
                                        decompressed.actual_size_bytes);
        slide_info.camera_position_gains.clear();
      } else {
        slide_info.camera_position_gains.clear();
        slide_info.camera_position_gains.reserve(
            static_cast<size_t>(npositions));

        const uint8_t* data = decompressed.data.data();
        for (int i = 0; i < npositions; ++i) {
          float gain;
          std::memcpy(&gain, data + (static_cast<size_t>(i) * sizeof(float)),
                      sizeof(float));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
          uint32_t temp;
          std::memcpy(&temp, data + (static_cast<size_t>(i) * sizeof(uint32_t)),
                      sizeof(uint32_t));
          temp = __builtin_bswap32(temp);
          std::memcpy(&gain, &temp, sizeof(float));
#endif
          slide_info.camera_position_gains.push_back(gain);
        }
      }
    } else {
      const int positions_x = slide_info.images_x / slide_info.image_divisions;
      const int positions_y = slide_info.images_y / slide_info.image_divisions;
      const int npositions = positions_x * positions_y;
      const int expected_size = 4 * npositions;

      if (compressed_metadata.size() != static_cast<size_t>(expected_size)) {
        LogGainMetadataSizeMismatchOnce(static_cast<size_t>(expected_size),
                                        compressed_metadata.size());
        slide_info.camera_position_gains.clear();
      } else {
        slide_info.camera_position_gains.clear();
        slide_info.camera_position_gains.reserve(
            static_cast<size_t>(npositions));

        const uint8_t* data = compressed_metadata.data();
        for (int i = 0; i < npositions; ++i) {
          float gain;
          std::memcpy(&gain, data + (static_cast<size_t>(i) * sizeof(float)),
                      sizeof(float));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
          uint32_t temp;
          std::memcpy(&temp, data + (static_cast<size_t>(i) * sizeof(uint32_t)),
                      sizeof(uint32_t));
          temp = __builtin_bswap32(temp);
          std::memcpy(&gain, &temp, sizeof(float));
#endif
          slide_info.camera_position_gains.push_back(gain);
        }
      }
    }
  }

  // Read position data from data file using helper
  std::vector<uint8_t> compressed_data;
  AIFOCORE_ASSIGN_OR_RETURN(compressed_data, mrxs::MrxsDataReader::ReadData(
                                                 datafile_path, offset, size));

  // Decompress if it's a stitching layer (zlib compressed)
  std::vector<uint8_t> position_data;
  if (is_stitching_layer) {
    // Check for zlib header
    if (compressed_data.size() >= 3 && compressed_data[0] == 0x78 &&
        compressed_data[1] == 0x9C && compressed_data[2] == 0xED) {
      // Calculate expected size
      const int positions_x = slide_info.images_x / slide_info.image_divisions;
      const int positions_y = slide_info.images_y / slide_info.image_divisions;
      const int npositions = positions_x * positions_y;
      const int expected_size = 9 * npositions;  // 9 bytes per position

      // Decompress
      AIFOCORE_ASSIGN_OR_RETURN(
          runtime::io::ZlibDecompressionResult decompressed,
          DecompressZlibWithActualSize(compressed_data.data(),
                                       compressed_data.size(),
                                       /*expected_size_hint=*/expected_size));
      if (decompressed.actual_size_bytes > static_cast<size_t>(expected_size)) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kResourceExhausted,
            aifocore::fmt::format(
                "Position buffer decompressed to {} bytes, expected at most {} "
                "bytes",
                decompressed.actual_size_bytes, expected_size));
      }
      position_data = std::move(decompressed.data);
      // Preserve old behavior: caller expects exactly `expected_size` bytes.
      position_data.resize(static_cast<size_t>(expected_size), 0);
    } else {
      position_data = std::move(compressed_data);
    }
  } else {
    position_data = std::move(compressed_data);
  }

  // Parse position buffer (9 bytes per position: flag, x, y)
  const int positions_x = slide_info.images_x / slide_info.image_divisions;
  const int positions_y = slide_info.images_y / slide_info.image_divisions;
  const int npositions = positions_x * positions_y;
  const int expected_size = 9 * npositions;

  if (position_data.size() != static_cast<size_t>(expected_size)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Position buffer size mismatch. Expected {}, got {}", expected_size,
            position_data.size()));
  }

  // Parse positions
  slide_info.camera_positions.clear();
  slide_info.camera_positions.reserve(npositions * 2);

  // Get level 0 concatenation factor for scaling
  // We need to handle the case where zoom_levels might be empty (though
  // ParseTiledLayers should catch this)
  int level_0_concat = 1;
  if (!slide_info.zoom_levels.empty()) {
    level_0_concat = 1 << slide_info.zoom_levels[0].downsample_exponent;
  }

  const uint8_t* p = position_data.data();
  for (int i = 0; i < npositions; ++i) {
    uint8_t flag = *p++;

    // Check flag (should be 0 or 1)
    if (flag & 0xFE) {
      std::cerr << "Unexpected flag value in position buffer: "
                << static_cast<int>(flag);
    }

    // Read x, y (little-endian int32)
    int32_t x, y;
    std::memcpy(&x, p, sizeof(x));
    p += sizeof(x);
    std::memcpy(&y, p, sizeof(y));
    p += sizeof(y);

    // Convert from little-endian if needed
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    x = __builtin_bswap32(x);
    y = __builtin_bswap32(y);
#endif

    // Scale by level 0 image_concat
    slide_info.camera_positions.push_back(x * level_0_concat);
    slide_info.camera_positions.push_back(y * level_0_concat);
  }

  slide_info.using_synthetic_positions = false;

  return aifocore::Status::OkStatus();
}

/// @brief Detect the type of associated data from magic bytes
///
/// Examines the first few bytes of data to determine its type (image, XML,
/// or binary). Checks for JPEG, PNG, BMP, XML, and zlib-compressed data
/// signatures. Falls back to heuristic analysis for text vs binary.
///
/// @param data Raw data bytes
/// @return Detected AssociatedDataType (kImage, kXml, kBinary, or kUnknown)
AssociatedDataType MrxsReader::DetectDataType(
    const std::vector<uint8_t>& data) {
  if (data.empty())
    return AssociatedDataType::kUnknown;

  // Check magic bytes
  if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
    return AssociatedDataType::kImage;  // JPEG
  }
  if (data.size() >= 4 && data[0] == 0x89 && data[1] == 0x50 &&
      data[2] == 0x4E && data[3] == 0x47) {
    return AssociatedDataType::kImage;  // PNG
  }
  if (data.size() >= 2 && data[0] == 0x42 && data[1] == 0x4D) {
    return AssociatedDataType::kImage;  // BMP
  }
  if (data.size() >= 5 && data[0] == '<' && data[1] == '?' && data[2] == 'x' &&
      data[3] == 'm' && data[4] == 'l') {
    return AssociatedDataType::kXml;  // XML
  }
  if (data.size() >= 3 && data[0] == 0x78 && data[1] == 0x9C) {
    return AssociatedDataType::kBinary;  // Zlib-compressed data
  }

  // Try to detect if it's text/XML by checking printable characters
  if (data.size() >= 10) {
    int printable_count = 0;
    for (size_t i = 0; i < std::min(data.size(), size_t(100)); ++i) {
      if ((data[i] >= 32 && data[i] <= 126) || data[i] == '\n' ||
          data[i] == '\r' || data[i] == '\t') {
        printable_count++;
      }
    }
    if (printable_count > 90) {         // >90% printable
      return AssociatedDataType::kXml;  // Likely text/XML
    }
  }

  return AssociatedDataType::kBinary;  // Default to binary
}

/// @brief Get list of available associated data items
///
/// Returns names of all non-hierarchical data items including preview images,
/// labels, XML metadata, and binary data.
///
/// @return Vector of data item names
std::vector<std::string> MrxsReader::GetAssociatedDataNames() const {
  std::vector<std::string> names;

  for (const auto& layer : slide_info_.nonhier_layers) {
    for (const auto& record : layer.records) {
      // Create unique name from layer and value
      std::string name =
          record.value_name.empty()
              ? aifocore::fmt::format("{}_{}", layer.name, record.layer_index)
              : record.value_name;
      names.push_back(name);
    }
  }

  return names;
}

std::vector<std::string> MrxsReader::GetNonImageAssociatedDataNames() const {
  std::vector<std::string> names;
  for (auto& name : GetAssociatedDataNames()) {
    if (name.find(mrxs::internal::kAssociatedImagePrefix) != 0) {
      names.push_back(std::move(name));
    }
  }
  return names;
}

/// @brief Get information about an associated data item without loading it
///
/// Returns metadata about a data item (size, type, compression) without
/// reading the actual data from disk.
///
/// @param name Data item name
/// @return Result containing AssociatedDataInfo or error
/// @retval NotFound if data item does not exist
/// @note Size and type are unknown until the data is actually loaded
aifocore::Result<AssociatedDataInfo> MrxsReader::GetAssociatedDataInfo(
    std::string_view name) const {
  // Find the record
  for (const auto& layer : slide_info_.nonhier_layers) {
    for (const auto& record : layer.records) {
      std::string record_name =
          record.value_name.empty()
              ? aifocore::fmt::format("{}_{}", layer.name, record.layer_index)
              : record.value_name;

      if (record_name == name) {
        AssociatedDataInfo info;
        info.name = record_name;
        info.description = aifocore::fmt::format("Layer: {}", layer.name);
        info.size_bytes = 0;         // Unknown until loaded
        info.is_compressed = false;  // Unknown until loaded
        info.compression_type = "unknown";
        info.type = AssociatedDataType::kUnknown;  // Unknown until loaded

        return info;
      }
    }
  }

  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated data not found: {}", name));
}

/// @brief Load associated data from disk
///
/// Reads and decodes a non-hierarchical data item. Automatically handles:
/// - Decompression (DEFLATE/zlib)
/// - Image decoding (JPEG/PNG/BMP)
/// - Type detection from magic bytes
///
/// @param name Data item name
/// @return Result containing AssociatedData with decoded content
/// @retval NotFound if data item doesn't exist or files can't be
/// opened
/// @retval Internal if reading or decoding fails
aifocore::Result<AssociatedData> MrxsReader::LoadAssociatedData(
    std::string_view name) const {
  // Find the record
  const mrxs::NonHierarchicalRecord* target_record = nullptr;
  for (const auto& layer : slide_info_.nonhier_layers) {
    for (const auto& record : layer.records) {
      std::string record_name =
          record.value_name.empty()
              ? aifocore::fmt::format("{}_{}", layer.name, record.layer_index)
              : record.value_name;

      if (record_name == name) {
        target_record = &record;
        break;
      }
    }
    if (target_record)
      break;
  }

  if (!target_record) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated data not found: {}", name));
  }

  // Read the record using index reader
  std::tuple<std::string, int64_t, int64_t> record_data;
  AIFOCORE_ASSIGN_OR_RETURN(record_data,
                            ReadNonHierRecord(target_record->record_index));

  auto [rel_path, offset, size] = record_data;
  fs::path datafile_path = dirname_ / rel_path;

  // Read data from file using helper
  std::vector<uint8_t> raw_data;
  AIFOCORE_ASSIGN_OR_RETURN(
      raw_data, mrxs::MrxsDataReader::ReadData(datafile_path, offset, size));

  // Detect data type
  AssociatedDataType type = DetectDataType(raw_data);

  // Check if compressed
  bool is_compressed = false;
  std::string compression_type = "none";
  std::vector<uint8_t> decompressed_data;

  if (raw_data.size() >= 3 && raw_data[0] == 0x78 && raw_data[1] == 0x9C) {
    is_compressed = true;
    compression_type = "zlib";

    // Try to decompress (use reasonable max size)
    size_t expected_size =
        raw_data.size() * 100;  // Assume up to 100x compression

    auto decompressed_or =
        DecompressZlib(raw_data.data(), raw_data.size(), expected_size);

    if (decompressed_or.ok()) {
      decompressed_data = std::move(*decompressed_or);
      // Re-detect type on decompressed data
      type = DetectDataType(decompressed_data);
    } else {
      // Decompression failed, keep as-is
      std::cerr << "Failed to decompress data for " << name << ": "
                << decompressed_or.status().ToString();
      decompressed_data = raw_data;
    }
  } else {
    decompressed_data = std::move(raw_data);
  }

  // Create AssociatedData
  AssociatedData result;
  result.info.name = std::string(name);
  result.info.description =
      aifocore::fmt::format("Layer: {}", target_record->layer_name);
  result.info.size_bytes = decompressed_data.size();
  result.info.is_compressed = is_compressed;
  result.info.compression_type = compression_type;
  result.info.type = type;

  // Parse data based on type
  switch (type) {
    case AssociatedDataType::kImage: {
      // Detect image format from magic bytes
      mrxs::MrxsImageFormat img_format = mrxs::MrxsImageFormat::kUnknown;
      if (decompressed_data.size() >= 2 && decompressed_data[0] == 0xFF &&
          decompressed_data[1] == 0xD8) {
        img_format = mrxs::MrxsImageFormat::kJpeg;
      } else if (decompressed_data.size() >= 4 &&
                 decompressed_data[0] == 0x89 && decompressed_data[1] == 0x50) {
        img_format = mrxs::MrxsImageFormat::kPng;
      } else if (decompressed_data.size() >= 2 &&
                 decompressed_data[0] == 0x42 && decompressed_data[1] == 0x4D) {
        img_format = mrxs::MrxsImageFormat::kBmp;
      } else if (decompressed_data.size() >= 4 &&
                 decompressed_data[0] == 0x49 && decompressed_data[1] == 0x49 &&
                 decompressed_data[2] == 0xBC) {
        // JPEG-XR/HD Photo magic: "II" + 0xBC followed by codec stream marker.
        img_format = mrxs::MrxsImageFormat::kJpegXr;
      }

      RGBImage decoded_image;
      AIFOCORE_ASSIGN_OR_RETURN(
          decoded_image,
          mrxs::internal::DecodeImage(decompressed_data, img_format));
      result.data = std::move(decoded_image);
      break;
    }

    case AssociatedDataType::kXml: {
      // Convert to string
      result.data =
          std::string(decompressed_data.begin(), decompressed_data.end());
      break;
    }

    default:
      // Keep as binary
      result.data = std::move(decompressed_data);
      break;
  }

  return result;
}

/// @brief Read raw compressed tile data from a data file
///
/// Delegates to MrxsDataReader helper class for data file reading.
/// This method now simply calls the static helper, which handles all
/// validation and file I/O.
///
/// @param tile Tile metadata containing file location and size
/// @return Result containing raw compressed data or error
/// @retval InvalidArgument if file number or params are invalid
/// @retval NotFound if data file cannot be opened
/// @retval Internal if seek or read operation fails
aifocore::Result<std::vector<uint8_t>> MrxsReader::ReadTileData(
    const mrxs::MiraxTileRecord& tile) const {
  // Delegate to data reader helper
  return mrxs::MrxsDataReader::ReadTileData(dirname_, tile,
                                            slide_info_.datafile_paths);
}

// ============================================================================
// Two-Stage Pipeline Implementation
// ============================================================================

/// @brief Prepare an execution plan for a tile request (two-stage pipeline)
///
/// First stage of the two-stage pipeline. Analyzes the request and builds
/// a plan describing all tiles to read, their locations, and transforms.
/// Does not perform I/O on image data (only metadata).
///
/// The plan can be:
/// - Inspected for cost estimation
/// - Cached for repeated use
/// - Modified before execution
/// - Executed asynchronously
///
/// @param request Tile request specifying region and level
/// @return Result containing TilePlan or error
/// @retval InvalidArgument if level is invalid
/// @note May perform I/O to build spatial index on first call for a level
aifocore::Result<core::TilePlan> MrxsReader::PrepareRequest(
    const core::TileRequest& request) const {
  // Use the plan builder helper to create the plan
  AIFOCORE_ASSIGN_OR_RETURN(const auto level_info, GetLevelInfo(request.level));
  AIFOCORE_ASSIGN_OR_RETURN(const auto spatial_index,
                            GetSpatialIndex(request.level));
  const MrxsPlanContext context{
      .slide_info = slide_info_,
      .level_info = level_info,
      .spatial_index = spatial_index,
  };
  return MrxsPlanBuilder::BuildPlan(request, context);
}

/// @brief Execute a prepared tile plan (two-stage pipeline)
///
/// Second stage of the two-stage pipeline. Executes a plan by reading tiles
/// from disk, decoding them, and painting to the output via Canvas.
///
/// The writer handles:
/// - Output buffer management
/// - Transform application
/// - Overlap averaging
///
/// @param plan Prepared tile plan from PrepareRequest
/// @param writer Tile writer for output
/// @return OkStatus on success, or error if any operation fails
/// @note Continues processing after individual tile failures (logs warnings)
aifocore::Status MrxsReader::ExecutePlan(const core::TilePlan& plan,
                                         runtime::Canvas& writer) const {
  // Use the tile executor helper to execute the plan
  const MrxsExecContext context(dirname_, slide_info_, GetCache());
  return MrxsTileExecutor::ExecutePlan(plan, context, writer);
}

}  // namespace fastslide
