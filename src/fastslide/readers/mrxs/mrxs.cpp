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

#include <cstring>
#include <filesystem>
#include <set>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/readers/mrxs/mrxs_constants.h"
#include "fastslide/readers/mrxs/mrxs_data_reader.h"
#include "fastslide/readers/mrxs/mrxs_decoder.h"
#include "fastslide/readers/mrxs/mrxs_index_reader.h"
#include "fastslide/readers/mrxs/mrxs_ini_parser.h"
#include "fastslide/readers/mrxs/mrxs_plan_builder.h"
#include "fastslide/readers/mrxs/mrxs_tile_executor.h"
#include "fastslide/readers/mrxs/spatial_index.h"
#include "fastslide/runtime/io/binary_utils.h"
#include "fastslide/runtime/io/file_reader.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/hash.h"

namespace fastslide {

// Forward declarations of helper functions
namespace {

/// @brief Parse tiled (hierarchical/zoom) layers from Slidedat.ini
absl::Status ParseTiledLayers(const mrxs::internal::IniFile& ini,
                              mrxs::SlideDataInfo& slide_info);

/// @brief Parse non-tiled (non-hierarchical) layers from Slidedat.ini
absl::Status ParseNonTiledLayers(const mrxs::internal::IniFile& ini,
                                 mrxs::SlideDataInfo& slide_info);

// Constants for MRXS format
constexpr std::string_view kMrxsExt = ".mrxs";
constexpr std::string_view kSlidedatIni = "Slidedat.ini";
constexpr std::string_view kIndexVersion = "01.02";

// INI file keys
constexpr std::string_view kGroupGeneral = "GENERAL";
constexpr std::string_view kKeySlideId = "SLIDE_ID";
constexpr std::string_view kKeyImageNumberX = "IMAGENUMBER_X";
constexpr std::string_view kKeyImageNumberY = "IMAGENUMBER_Y";
constexpr std::string_view kKeyObjectiveMagnification =
    "OBJECTIVE_MAGNIFICATION";
constexpr std::string_view kKeyCameraImageDivisionsPerSide =
    "CameraImageDivisionsPerSide";

constexpr std::string_view kGroupHierarchical = "HIERARCHICAL";
constexpr std::string_view kKeyHierCount = "HIER_COUNT";
constexpr std::string_view kKeyIndexFile = "INDEXFILE";
constexpr std::string_view kValueSlideZoomLevel = "Slide zoom level";

// Hierarchical format strings
constexpr std::string_view kKeyHierLevelName = "HIER_%d_NAME";
constexpr std::string_view kKeyHierLevelCount = "HIER_%d_COUNT";
constexpr std::string_view kKeyHierLevelValSection = "HIER_%d_VAL_%d_SECTION";

// Non-hierarchical format strings
constexpr std::string_view kKeyNonHierCount = "NONHIER_COUNT";
constexpr std::string_view kKeyNonHierName = "NONHIER_%d_NAME";
constexpr std::string_view kKeyNonHierLevelCount = "NONHIER_%d_COUNT";
constexpr std::string_view kKeyNonHierVal = "NONHIER_%d_VAL_%d";
constexpr std::string_view kKeyNonHierValSection = "NONHIER_%d_VAL_%d_SECTION";

constexpr std::string_view kGroupDatafile = "DATAFILE";
constexpr std::string_view kKeyFileCount = "FILE_COUNT";
constexpr std::string_view kKeyDataFile = "FILE_%d";

constexpr std::string_view kKeyOverlapX = "OVERLAP_X";
constexpr std::string_view kKeyOverlapY = "OVERLAP_Y";
constexpr std::string_view kKeyMppX = "MICROMETER_PER_PIXEL_X";
constexpr std::string_view kKeyMppY = "MICROMETER_PER_PIXEL_Y";
constexpr std::string_view kKeyImageFormat = "IMAGE_FORMAT";
constexpr std::string_view kKeyImageFillColorBgr = "IMAGE_FILL_COLOR_BGR";
constexpr std::string_view kKeyDigitizerWidth = "DIGITIZER_WIDTH";
constexpr std::string_view kKeyDigitizerHeight = "DIGITIZER_HEIGHT";
constexpr std::string_view kKeyImageConcatFactor = "IMAGE_CONCAT_FACTOR";

/// @brief Parse image format string from INI file
///
/// Converts a string representation of image format (from Slidedat.ini) to
/// the corresponding MrxsImageFormat enum value.
///
/// @param format_str Format string from INI file ("JPEG", "PNG", or "BMP")
/// @return StatusOr containing parsed format or error
/// @retval absl::InvalidArgumentError if format string is unknown
absl::StatusOr<mrxs::MrxsImageFormat> ParseImageFormat(
    std::string_view format_str) {
  if (format_str == "JPEG") {
    return mrxs::MrxsImageFormat::kJpeg;
  } else if (format_str == "PNG") {
    LOG(WARNING) << "Parsing PNG image format."
                 << "This path is not properly tested nor optimized!";
    return mrxs::MrxsImageFormat::kPng;
  } else if (format_str == "BMP") {
    LOG(WARNING) << "Parsing BMP image format."
                 << "This path is not properly tested nor optimized!";
    return mrxs::MrxsImageFormat::kBmp;
  }
  return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                     absl::StrFormat("Unknown image format: %s", format_str));
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
    : dirname_(std::move(dirname)),
      slide_info_(slide_info),
      cache_(nullptr) {  // Initialize cache to nullptr
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
/// @return StatusOr containing unique pointer to MrxsReader or error
/// @retval absl::InvalidArgumentError if file extension is not .mrxs
/// @retval absl::NotFoundError if file or required data files don't exist
/// @retval absl::InternalError if parsing fails
absl::StatusOr<std::unique_ptr<MrxsReader>> MrxsReader::Create(
    fs::path filename) {
  return CreateImpl(filename);
}

absl::Status MrxsReader::ValidateInput(const fs::path& filename) {
  // Verify filename has .mrxs extension
  if (filename.extension() != kMrxsExt) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("File does not have %s extension", kMrxsExt));
  }

  // Check file exists
  if (!fs::exists(filename)) {
    return MAKE_STATUS(
        absl::StatusCode::kNotFound,
        absl::StrFormat("File does not exist: %s", filename.string()));
  }

  // Get directory name (remove .mrxs extension)
  fs::path dirname = filename.parent_path() / filename.stem();

  // Check if Slidedat.ini exists
  fs::path slidedat_path = dirname / kSlidedatIni;
  if (!fs::exists(slidedat_path)) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       absl::StrFormat("%s does not exist: %s", kSlidedatIni,
                                       slidedat_path.string()));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<MrxsReader>> MrxsReader::CreateReaderImpl(
    const fs::path& filename) {
  // Get directory name (remove .mrxs extension)
  fs::path dirname = filename.parent_path() / filename.stem();
  fs::path slidedat_path = dirname / kSlidedatIni;

  // Read Slidedat.ini
  mrxs::SlideDataInfo slide_info;
  ASSIGN_OR_RETURN(slide_info, ReadSlidedatIni(slidedat_path, dirname));

  // IMPORTANT: Camera positions MUST be read during initialization!
  // They are required for accurate tile positioning when reading image data.
  RETURN_IF_ERROR(ReadCameraPositions(dirname, slide_info),
                  "Failed to read camera positions");

  auto reader = std::unique_ptr<MrxsReader>(
      new MrxsReader(std::move(dirname), std::move(slide_info)));

  RETURN_IF_ERROR(reader->InitializeProperties(),
                  "Failed to initialize properties");

  return reader;
}

namespace {

/// @brief Parse tiled (hierarchical/zoom) layers from Slidedat.ini
///
/// Lazy-loads metadata about the multi-resolution pyramid levels without
/// reading any actual tile data from disk. Each zoom level contains information
/// about tile dimensions, overlaps, compression format, and MPP values.
///
/// This method only reads INI file metadata - actual tile indices are read
/// later via ReadLevelTiles() when needed for spatial indexing or region
/// reading.
///
/// @param ini Parsed INI file
/// @param slide_info Slide information to update with tiled layers
/// @return OkStatus on success or error status
/// @retval absl::InvalidArgumentError if required keys are missing or invalid
absl::Status ParseTiledLayers(const mrxs::internal::IniFile& ini,
                              mrxs::SlideDataInfo& slide_info) {
  // Read hierarchical section for levels
  if (!ini.HasSection(kGroupHierarchical)) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Missing section: %s", kGroupHierarchical));
  }

  int hier_count;
  ASSIGN_OR_RETURN(hier_count, ini.GetInt(kGroupHierarchical, kKeyHierCount));

  // Find slide zoom level index
  int slide_zoom_level_index = -1;
  for (int i = 0; i < hier_count; ++i) {
    std::string key = absl::StrFormat(kKeyHierLevelName, i);
    auto value = ini.GetString(kGroupHierarchical, key);
    if (value.ok() && *value == kValueSlideZoomLevel) {
      slide_zoom_level_index = i;
      break;
    }
  }

  if (slide_zoom_level_index == -1) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Cannot find slide zoom level");
  }

  ASSIGN_OR_RETURN(slide_info.index_filename,
                   ini.GetString(kGroupHierarchical, kKeyIndexFile));

  // Get number of zoom levels
  std::string zoom_count_key =
      absl::StrFormat(kKeyHierLevelCount, slide_zoom_level_index);
  int zoom_levels;
  ASSIGN_OR_RETURN(zoom_levels, ini.GetInt(kGroupHierarchical, zoom_count_key));

  // Read zoom level section names
  std::vector<std::string> section_names;
  for (int i = 0; i < zoom_levels; ++i) {
    std::string key =
        absl::StrFormat(kKeyHierLevelValSection, slide_zoom_level_index, i);
    std::string section_name;
    ASSIGN_OR_RETURN(section_name, ini.GetString(kGroupHierarchical, key));
    section_names.push_back(section_name);
  }

  // Read each zoom level
  for (const auto& section : section_names) {
    if (!ini.HasSection(section)) {
      continue;  // Skip if section doesn't exist
    }

    mrxs::SlideZoomLevel level;
    level.section_name = section;

    ASSIGN_OR_RETURN(level.x_overlap_pixels,
                     ini.GetDouble(section, kKeyOverlapX));
    ASSIGN_OR_RETURN(level.y_overlap_pixels,
                     ini.GetDouble(section, kKeyOverlapY));
    ASSIGN_OR_RETURN(level.mpp_x, ini.GetDouble(section, kKeyMppX));
    ASSIGN_OR_RETURN(level.mpp_y, ini.GetDouble(section, kKeyMppY));
    std::string format_str;
    ASSIGN_OR_RETURN(format_str, ini.GetString(section, kKeyImageFormat));
    ASSIGN_OR_RETURN(level.image_format, ParseImageFormat(format_str));
    auto fill_color = ini.GetInt(section, kKeyImageFillColorBgr);
    level.background_color_rgb =
        fill_color.ok() ? static_cast<uint32_t>(*fill_color) : 0xFFFFFFFF;

    ASSIGN_OR_RETURN(level.image_width,
                     ini.GetInt(section, kKeyDigitizerWidth));

    ASSIGN_OR_RETURN(level.image_height,
                     ini.GetInt(section, kKeyDigitizerHeight));

    auto concat_exp = ini.GetInt(section, kKeyImageConcatFactor);
    level.downsample_exponent = concat_exp.ok() ? *concat_exp : 0;

    slide_info.zoom_levels.push_back(level);
  }

  return absl::OkStatus();
}

}  // namespace

/// @brief Read and parse the Slidedat.ini metadata file
///
/// Parses the MRXS metadata file containing slide configuration, pyramid
/// structure, and level parameters. Extracts all necessary information for
/// initializing the reader.
///
/// @param slidedat_path Path to Slidedat.ini file
/// @param dirname Path to MRXS directory for cache keys
/// @return StatusOr containing parsed SlideDataInfo or error
/// @retval absl::NotFoundError if required sections or keys are missing
/// @retval absl::InvalidArgumentError if values cannot be parsed
absl::StatusOr<mrxs::SlideDataInfo> MrxsReader::ReadSlidedatIni(
    const fs::path& slidedat_path, const fs::path& dirname) {
  mrxs::internal::IniFile ini;
  ASSIGN_OR_RETURN(ini, mrxs::internal::IniFile::Load(slidedat_path),
                   "Failed to load Slidedat.ini");

  mrxs::SlideDataInfo info;
  info.dirname = dirname.string();  // Store dirname for cache keys

  // Read general section
  if (!ini.HasSection(kGroupGeneral)) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Missing section: %s", kGroupGeneral));
  }

  ASSIGN_OR_RETURN(info.slide_id, ini.GetString(kGroupGeneral, kKeySlideId));
  ASSIGN_OR_RETURN(info.images_x, ini.GetInt(kGroupGeneral, kKeyImageNumberX));
  ASSIGN_OR_RETURN(info.images_y, ini.GetInt(kGroupGeneral, kKeyImageNumberY));

  ASSIGN_OR_RETURN(info.objective_magnification,
                   ini.GetInt(kGroupGeneral, kKeyObjectiveMagnification));

  auto image_divisions =
      ini.GetInt(kGroupGeneral, kKeyCameraImageDivisionsPerSide);
  info.image_divisions = image_divisions.ok() ? *image_divisions : 1;

  // Parse tiled (hierarchical/zoom) layers
  RETURN_IF_ERROR(ParseTiledLayers(ini, info), "Failed to parse tiled layers");
  // Parse non-tiled (non-hierarchical) layers
  RETURN_IF_ERROR(ParseNonTiledLayers(ini, info),
                  "Failed to parse non-tiled layers");

  // Read datafile section
  if (!ini.HasSection(kGroupDatafile)) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Missing section: %s", kGroupDatafile));
  }

  int file_count;
  ASSIGN_OR_RETURN(file_count, ini.GetInt(kGroupDatafile, kKeyFileCount));

  for (int i = 0; i < file_count; ++i) {
    std::string key = absl::StrFormat(kKeyDataFile, i);
    std::string file_path;
    ASSIGN_OR_RETURN(file_path, ini.GetString(kGroupDatafile, key));
    info.datafile_paths.push_back(file_path);
  }

  return info;
}

/// @brief Initialize slide properties from parsed metadata
///
/// Populates the SlideProperties structure with metadata extracted from
/// Slidedat.ini. Sets microns per pixel (MPP), objective magnification,
/// scanner model, and calculates slide bounds.
absl::Status MrxsReader::InitializeProperties() {
  // Set basic properties
  if (!slide_info_.zoom_levels.empty()) {
    const auto& level0 = slide_info_.zoom_levels[0];
    properties_.mpp = {level0.mpp_x, level0.mpp_y};
    properties_.objective_magnification =
        static_cast<double>(slide_info_.objective_magnification);
  }
  properties_.scanner_model = "3DHISTECH";

  // Calculate bounds from level 0 tiles
  ASSIGN_OR_RETURN(properties_.bounds, CalculateBounds());

  return absl::OkStatus();
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
/// @return StatusOr containing SlideBounds or error
absl::StatusOr<SlideBounds> MrxsReader::CalculateBounds() {
  // Read tiles directly (no spatial index needed)
  std::vector<mrxs::MiraxTileRecord> tiles;
  ASSIGN_OR_RETURN(tiles, ReadLevelTiles(0),
                   "Failed to read tiles for bounds calculation");

  if (tiles.empty()) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "No tiles found at level 0");
  }

  // Get level 0 dimensions for clamping
  LevelInfo level0;
  ASSIGN_OR_RETURN(level0, GetLevelInfo(0), "Failed to get level 0 info");
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
    LOG(INFO) << "CalculateBounds: No active tiles found";
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

    // Calculate how many camera positions are represented in each image
    // At high resolution: positions_per_image = 1 (one image per camera
    // position) At low resolution: positions_per_image > 1 (multiple positions
    // per image)
    const int camera_positions_per_image = std::max(
        1, level_params.concatenation_factor / slide_info_.image_divisions);

    // Tile count divisor determines whether we reduce tile count or tile size
    // when going to lower resolutions. It bottoms out at image_divisions.
    level_params.grid_divisor = std::min(level_params.concatenation_factor,
                                         slide_info_.image_divisions);

    // Each stored image may contain multiple logical tiles that need extraction
    level_params.subtiles_per_stored_image = camera_positions_per_image;

    // Each tile typically represents one camera position at high resolution
    level_params.camera_positions_per_tile = 1;

    // Calculate dimensions of each logical tile within the stored image
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
/// @return StatusOr containing LevelInfo or error
/// @retval absl::InvalidArgumentError if level is out of range
absl::StatusOr<LevelInfo> MrxsReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= GetLevelCount()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid level: %d", level));
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
/// MRXS slides are always RGB, so this returns metadata for red, green, and
/// blue channels.
///
/// @return Vector of 3 ChannelMetadata structs (R, G, B)
std::vector<ChannelMetadata> MrxsReader::GetChannelMetadata() const {
  // MRXS is RGB, so return 3 channels
  ChannelMetadata red, green, blue;
  red.name = "Red";
  red.color = ColorRGB{255, 0, 0};

  green.name = "Green";
  green.color = ColorRGB{0, 255, 0};

  blue.name = "Blue";
  blue.color = ColorRGB{0, 0, 255};

  return {red, green, blue};
}

/// @brief Get names of associated images (label, macro, thumbnail)
///
/// MRXS format stores associated images in non-hierarchical records rather
/// than as traditional associated images. Use GetAssociatedDataNames() instead
/// for MRXS-specific associated data.
///
/// @return Empty vector (use GetAssociatedDataNames for MRXS data)
std::vector<std::string> MrxsReader::GetAssociatedImageNames() const {
  // MRXS doesn't typically have associated images like macro/label
  return {};
}

/// @brief Get dimensions of an associated image
///
/// MRXS uses non-hierarchical records for associated data. Use
/// LoadAssociatedData() instead.
///
/// @param name Image name
/// @return Always returns NotFoundError (not implemented for MRXS)
absl::StatusOr<ImageDimensions> MrxsReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  return MAKE_STATUS(absl::StatusCode::kNotFound,
                     absl::StrFormat("Associated image not found: %s", name));
}

/// @brief Read an associated image
///
/// MRXS uses non-hierarchical records for associated data. Use
/// LoadAssociatedData() instead.
///
/// @param name Image name
/// @return Always returns NotFoundError (not implemented for MRXS)
absl::StatusOr<RGBImage> MrxsReader::ReadAssociatedImage(
    std::string_view name) const {
  return MAKE_STATUS(absl::StatusCode::kNotFound,
                     absl::StrFormat("Associated image not found: %s", name));
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
  meta[std::string(MetadataKeys::kChannels)] = static_cast<size_t>(3);  // RGB

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
/// @return StatusOr containing hexadecimal hash string or error
absl::StatusOr<std::string> MrxsReader::GetQuickHash() const {
  // OpenSlide-compatible quickhash: hash Slidedat.ini + all lowest-res tile
  // data
  QuickHashBuilder hasher;

  // Hash the Slidedat.ini file
  fs::path slidedat_path = dirname_ / kSlidedatIni;
  RETURN_IF_ERROR(hasher.HashFile(slidedat_path),
                  "Failed to hash Slidedat.ini");

  // Hash all tiles from the lowest resolution level (highest level index)
  const int lowest_res_level = GetLevelCount() - 1;
  if (lowest_res_level < 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "No pyramid levels available");
  }

  std::vector<mrxs::MiraxTileRecord> tiles_or;
  ASSIGN_OR_RETURN(tiles_or, ReadLevelTiles(lowest_res_level),
                   "Failed to read tiles for quickhash");

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
      return MAKE_STATUS(
          absl::StatusCode::kInvalidArgument,
          absl::StrFormat("Invalid data file number %d for tile at (%d, %d)",
                          tile.data_file_number, tile.x, tile.y));
    }

    // Hash the raw compressed data directly from the data file
    fs::path data_file_path =
        dirname_ / slide_info_.datafile_paths[tile.data_file_number];
    RETURN_IF_ERROR(
        hasher.HashFilePart(data_file_path, tile.offset, tile.length),
        absl::StrFormat("Failed to hash tile data at (%d, %d) from %s", tile.x,
                        tile.y, data_file_path.string()));

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
/// This now delegates to the two-stage pipeline which uses WeightedTileWriter
/// with Magic Kernel resampling for subpixel-accurate positioning.
///
/// @param level Pyramid level (0 = highest resolution)
/// @param x X coordinate (fractional, in level-native space)
/// @param y Y coordinate (fractional, in level-native space)
/// @param width Width in pixels
/// @param height Height in pixels
/// @return StatusOr containing RGB image or error
/// @retval absl::InvalidArgumentError if level is invalid
absl::StatusOr<RGBImage> MrxsReader::ReadRegionFractional(
    int level, double x, double y, uint32_t width, uint32_t height) const {
  // Validate level
  // TODO: Check how different this is and if it can't be merged into the CRTP
  // pattern
  if (level < 0 || level >= GetLevelCount()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid level: %d", level));
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
  ASSIGN_OR_RETURN(plan, PrepareRequest(request),
                   "could not create tiling plan");

  // Use unified TileWriter for MRXS (automatic strategy selection)
  const auto& zoom_level = slide_info_.zoom_levels[level];
  // Why is this done later?
  runtime::TileWriter::BackgroundColor bg{
      static_cast<uint8_t>((zoom_level.background_color_rgb >> 16) & 0xFF),
      static_cast<uint8_t>((zoom_level.background_color_rgb >> 8) & 0xFF),
      static_cast<uint8_t>(zoom_level.background_color_rgb & 0xFF)};

  runtime::TileWriter writer(plan.output.dimensions[0],
                             plan.output.dimensions[1], bg,
                             true);  // Enable blending for MRXS

  // Execute the plan
  RETURN_IF_ERROR(ExecutePlan(plan, writer), "could not execute plan");
  RETURN_IF_ERROR(writer.Finalize(), "could not finalize writing");

  runtime::TileWriter::OutputImage image;
  ASSIGN_OR_RETURN(image, writer.GetOutput(), "could not get writer output");

  // Convert Image to RGBImage for backward compatibility
  // TODO: Get rid of this
  RGBImage rgb_result(image.GetDimensions(), ImageFormat::kRGB,
                      DataType::kUInt8);
  std::memcpy(rgb_result.GetData(), image.GetData(),
              image.GetDataVector().size());

  return rgb_result;
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
/// @return StatusOr containing shared pointer to spatial index or error
/// @retval absl::InvalidArgumentError if level is out of range
/// @note First call for a level performs I/O to read index file
absl::StatusOr<std::shared_ptr<mrxs::MrxsSpatialIndex>>
MrxsReader::GetSpatialIndex(int level) const {
  absl::MutexLock lock(&spatial_index_mutex_);

  if (spatial_indices_[level]) {
    return spatial_indices_[level];
  }

  // Build spatial index
  std::vector<mrxs::MiraxTileRecord> tiles;
  ASSIGN_OR_RETURN(tiles, ReadLevelTiles(level),
                   absl::StrFormat("could not read tiles at level: %d", level));

  std::unique_ptr<mrxs::MrxsSpatialIndex> index;
  ASSIGN_OR_RETURN(index, mrxs::MrxsSpatialIndex::Build(
                              tiles, level_params_[level], level, slide_info_));

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
absl::StatusOr<std::vector<mrxs::MiraxTileRecord>> MrxsReader::ReadLevelTiles(
    int level_index) const {
  if (level_index < 0 || level_index >= GetLevelCount()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       absl::StrFormat("Invalid level index: %d", level_index));
  }

  // Open index file using helper class
  fs::path index_path = dirname_ / slide_info_.index_filename;
  mrxs::MrxsIndexReader index_reader;
  ASSIGN_OR_RETURN(index_reader,
                   mrxs::MrxsIndexReader::Open(index_path, slide_info_),
                   "Failed to open index file");

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
absl::StatusOr<std::tuple<std::string, int64_t, int64_t>>
MrxsReader::ReadNonHierRecord(int record_index) const {
  // Open index file using helper class
  fs::path index_path = dirname_ / slide_info_.index_filename;
  mrxs::MrxsIndexReader index_reader;
  ASSIGN_OR_RETURN(index_reader,
                   mrxs::MrxsIndexReader::Open(index_path, slide_info_),
                   "Failed to open index file");

  // Delegate to index reader
  mrxs::NonHierRecordData record_data;
  ASSIGN_OR_RETURN(record_data, index_reader.ReadNonHierRecord(record_index),
                   "Failed to read non-hierarchical record");

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
/// @retval absl::NotFoundError if position data file cannot be opened
/// @retval absl::InternalError if file operations fail
/// @retval absl::InvalidArgumentError if data format is invalid
/// @note Handles both uncompressed and DEFLATE-compressed position data
/// @note Uses position layer metadata populated during ParseNonTiledLayers()
/// @note If position data unavailable, using_synthetic_positions is already set
absl::Status MrxsReader::ReadCameraPositions(const fs::path& dirname,
                                             mrxs::SlideDataInfo& slide_info) {
  // Check if position data is available (determined during INI parsing)
  if (slide_info.using_synthetic_positions ||
      slide_info.position_layer_record_offset == -1) {
    // No position data found, synthetic positions already set
    return absl::OkStatus();
  }

  // Use pre-populated position layer metadata
  const int position_record = slide_info.position_layer_record_offset;
  const bool is_stitching_layer = slide_info.position_layer_compressed;

  // Open index file using RAII wrapper
  fs::path index_path = dirname / slide_info.index_filename;
  FileReader indexfile;
  ASSIGN_OR_RETURN(
      indexfile, FileReader::Open(index_path, "rb"),
      absl::StrFormat("Cannot open index file: %s", index_path.string()));

  // Calculate nonhier root
  const int64_t hier_root = strlen("01.02") + slide_info.slide_id.length();

  const int64_t nonhier_root = hier_root + 4;

  // Read the position record inline (avoiding temp reader construction)
  // Seek to nonhier root
  RETURN_IF_ERROR(indexfile.Seek(nonhier_root), "Cannot seek to nonhier root");

  // Read pointer
  int32_t ptr_32;
  ASSIGN_OR_RETURN(ptr_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read nonhier pointer");
  int64_t ptr = ptr_32;

  // Seek to record pointer
  RETURN_IF_ERROR(indexfile.Seek(ptr + 4 * position_record),
                  "Cannot seek to record pointer");

  // Read pointer to record data
  int32_t record_ptr_32;
  ASSIGN_OR_RETURN(record_ptr_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read record pointer");
  int64_t record_ptr = record_ptr_32;

  // Seek to record data
  RETURN_IF_ERROR(indexfile.Seek(record_ptr), "Cannot seek to record data");

  // Read initial 0
  int32_t zero_value_32;
  ASSIGN_OR_RETURN(zero_value_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read initial value");
  int64_t zero_value = zero_value_32;

  if (zero_value != 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Expected 0 at beginning of nonhier record, got %d",
                        zero_value));
  }

  // Read data pointer
  int32_t data_ptr_32;
  ASSIGN_OR_RETURN(data_ptr_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read data pointer");
  int64_t data_ptr = data_ptr_32;

  // Seek to data page
  RETURN_IF_ERROR(indexfile.Seek(data_ptr), "Cannot seek to data page");

  // Read page length
  int32_t page_len_32;
  ASSIGN_OR_RETURN(page_len_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read page length");
  int64_t page_len = page_len_32;

  if (page_len < 1) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Expected at least one data item in position data page");
  }

  // Skip next pointer and two zeros
  for (int i = 0; i < 3; ++i) {
    int32_t skip_32;
    ASSIGN_OR_RETURN(skip_32, ReadLeInt32(indexfile.Get()),
                     "Cannot read skip value");
    (void)skip_32;  // Intentionally unused
  }

  // Read offset, size, fileno for first data item (position data)
  int32_t offset_32;
  ASSIGN_OR_RETURN(offset_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read position data offset");
  int64_t offset = offset_32;

  int32_t size_32;
  ASSIGN_OR_RETURN(size_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read position data size");
  int64_t size = size_32;

  int32_t fileno_32;
  ASSIGN_OR_RETURN(fileno_32, ReadLeInt32(indexfile.Get()),
                   "Cannot read position data file number");
  int64_t fileno = fileno_32;

  if (fileno < 0 ||
      fileno >= static_cast<int>(slide_info.datafile_paths.size())) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Invalid datafile number: %d (must be 0-%zu)", fileno,
                        slide_info.datafile_paths.size() - 1));
  }

  // Construct full path to datafile
  fs::path datafile_path = dirname / slide_info.datafile_paths[fileno];

  // Check for second data item (additional metadata per camera position)
  // MRXS slides with version ≥ 2.2 may have this
  if (page_len >= 2) {
    // Skip reserved fields (2 int32s)
    for (int i = 0; i < 2; ++i) {
      int32_t reserved_32;
      ASSIGN_OR_RETURN(reserved_32, ReadLeInt32(indexfile.Get()),
                       "Failed to read reserved field in gain metadata");
      (void)reserved_32;  // Intentionally unused
    }

    // Read second data item location
    int32_t offset2_32;
    ASSIGN_OR_RETURN(offset2_32, ReadLeInt32(indexfile.Get()),
                     "Failed to read gain metadata offset");
    int64_t offset2 = offset2_32;

    int32_t size2_32;
    ASSIGN_OR_RETURN(size2_32, ReadLeInt32(indexfile.Get()),
                     "Failed to read gain metadata size");
    int64_t size2 = size2_32;

    int32_t fileno2_32;
    ASSIGN_OR_RETURN(fileno2_32, ReadLeInt32(indexfile.Get()),
                     "Failed to read gain metadata file number");
    int64_t fileno2 = fileno2_32;

    // Validate file number
    if (fileno2 < 0 ||
        fileno2 >= static_cast<int>(slide_info.datafile_paths.size())) {
      return MAKE_STATUS(
          absl::StatusCode::kInvalidArgument,
          absl::StrFormat("Invalid gain metadata file number: %d", fileno2));
    }

    // Read metadata from data file using helper
    fs::path datafile2_path = dirname / slide_info.datafile_paths[fileno2];
    std::vector<uint8_t> compressed_metadata;
    ASSIGN_OR_RETURN(
        compressed_metadata,
        mrxs::MrxsDataReader::ReadData(datafile2_path, offset2, size2),
        "Failed to read gain metadata");

    // Decompress if needed (zlib header)
    std::vector<uint8_t> metadata_per_position;
    if (compressed_metadata.size() >= 2 && compressed_metadata[0] == 0x78 &&
        compressed_metadata[1] == 0x9C) {
      const int positions_x = slide_info.images_x / slide_info.image_divisions;
      const int positions_y = slide_info.images_y / slide_info.image_divisions;
      const int npositions = positions_x * positions_y;
      const int expected_size = 4 * npositions;

      ASSIGN_OR_RETURN(
          metadata_per_position,
          DecompressZlib(compressed_metadata.data(), compressed_metadata.size(),
                         expected_size),
          "Failed to decompress gain metadata");
    } else {
      metadata_per_position = std::move(compressed_metadata);
    }

    // Store the gain values for use during tile reading
    if (metadata_per_position.size() >= 4) {
      const size_t num_values = metadata_per_position.size() / 4;
      slide_info.camera_position_gains.clear();
      slide_info.camera_position_gains.reserve(num_values);

      for (size_t i = 0; i < num_values; ++i) {
        const uint8_t* ptr = metadata_per_position.data() + (i * 4);
        float gain;
        std::memcpy(&gain, ptr, sizeof(float));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        uint32_t temp;
        std::memcpy(&temp, ptr, sizeof(uint32_t));
        temp = __builtin_bswap32(temp);
        std::memcpy(&gain, &temp, sizeof(float));
#endif
        slide_info.camera_position_gains.push_back(gain);
      }
    }
  }

  // Read position data from data file using helper
  std::vector<uint8_t> compressed_data;
  ASSIGN_OR_RETURN(compressed_data,
                   mrxs::MrxsDataReader::ReadData(datafile_path, offset, size),
                   "Failed to read position data");

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
      ASSIGN_OR_RETURN(position_data,
                       DecompressZlib(compressed_data.data(),
                                      compressed_data.size(), expected_size),
                       "Failed to decompress position data");
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
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Position buffer size mismatch. Expected %d, got %zu",
                        expected_size, position_data.size()));
  }

  // Parse positions
  slide_info.camera_positions.clear();
  slide_info.camera_positions.reserve(npositions * 2);

  // Get level 0 concatenation factor for scaling
  int level_0_concat = 1 << slide_info.zoom_levels[0].downsample_exponent;

  const uint8_t* p = position_data.data();
  for (int i = 0; i < npositions; ++i) {
    uint8_t flag = *p++;

    // Check flag (should be 0 or 1)
    if (flag & 0xFE) {
      LOG(WARNING) << "Unexpected flag value in position buffer: "
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

  return absl::OkStatus();
}

namespace {

/// @brief Parse non-tiled (non-hierarchical) layer metadata from Slidedat.ini
///
/// Lazy-loads metadata about associated data (label, macro, thumbnails,
/// position data, etc.) without reading the actual data from disk. The data is
/// loaded on demand via LoadAssociatedData() or ReadCameraPositions().
///
/// This method only reads INI file metadata - no actual image data or binary
/// data is loaded, keeping initialization fast.
///
/// Additionally detects and stores position layer information
/// (VIMSLIDE_POSITION_BUFFER or StitchingIntensityLayer) and sets
/// using_synthetic_positions flag accordingly.
///
/// @param ini Parsed INI file
/// @param slide_info Slide information to update with non-tiled layer metadata
/// @return OkStatus on success or error status
absl::Status ParseNonTiledLayers(const mrxs::internal::IniFile& ini,
                                 mrxs::SlideDataInfo& slide_info) {

  // Get NONHIER_COUNT (optional - older MRXS files may not have
  // non-hierarchical layers)
  auto nonhier_count_or = ini.GetInt(kGroupHierarchical, kKeyNonHierCount);
  if (!nonhier_count_or.ok()) {
    // No nonhier sections - use synthetic positions (valid for older slides)
    slide_info.using_synthetic_positions = true;
    return absl::OkStatus();
  }

  const int nonhier_count = *nonhier_count_or;

  slide_info.nonhier_layers.clear();
  int record_offset = 0;

  // Track whether we find position data
  bool found_position_layer = false;

  for (int i = 0; i < nonhier_count; ++i) {
    mrxs::NonHierarchicalLayer layer;

    // Get layer name
    std::string name_key = absl::StrFormat(kKeyNonHierName, i);
    ASSIGN_OR_RETURN(
        layer.name, ini.GetString(kGroupHierarchical, name_key),
        absl::StrFormat("Missing non-hierarchical layer name for index %d", i));

    // Get count
    std::string count_key = absl::StrFormat(kKeyNonHierLevelCount, i);
    ASSIGN_OR_RETURN(
        layer.count, ini.GetInt(kGroupHierarchical, count_key),
        absl::StrFormat("Missing count for non-hierarchical layer %s",
                        layer.name));
    layer.record_offset = record_offset;

    // Get individual record information
    for (int j = 0; j < layer.count; ++j) {
      mrxs::NonHierarchicalRecord record;
      record.layer_name = layer.name;
      record.record_index = record_offset + j;
      record.layer_index = j;

      // Get value name (optional field - may not exist for all records)
      std::string val_key = absl::StrFormat(kKeyNonHierVal, i, j);
      auto val = ini.GetString(kGroupHierarchical, val_key);
      if (val.ok()) {
        record.value_name = *val;
      }

      // Get section name (optional field - may not exist for all records)
      std::string section_key = absl::StrFormat(kKeyNonHierValSection, i, j);
      auto section = ini.GetString(kGroupHierarchical, section_key);
      if (section.ok()) {
        record.section_name = *section;
      }

      layer.records.push_back(record);
    }

    // Check if this is a position data layer
    if (layer.name == "VIMSLIDE_POSITION_BUFFER") {
      slide_info.position_layer_name = layer.name;
      slide_info.position_layer_record_offset = record_offset;
      slide_info.position_layer_compressed = false;
      found_position_layer = true;
    } else if (layer.name == "StitchingIntensityLayer") {
      slide_info.position_layer_name = layer.name;
      slide_info.position_layer_record_offset = record_offset;
      slide_info.position_layer_compressed = true;
      found_position_layer = true;
    }

    slide_info.nonhier_layers.push_back(layer);
    record_offset += layer.count;
  }

  // Set synthetic positions flag based on whether we found position data
  slide_info.using_synthetic_positions = !found_position_layer;

  return absl::OkStatus();
}

}  // namespace

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
              ? absl::StrFormat("%s_%d", layer.name, record.layer_index)
              : record.value_name;
      names.push_back(name);
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
/// @return StatusOr containing AssociatedDataInfo or error
/// @retval absl::NotFoundError if data item does not exist
/// @note Size and type are unknown until the data is actually loaded
absl::StatusOr<AssociatedDataInfo> MrxsReader::GetAssociatedDataInfo(
    std::string_view name) const {
  // Find the record
  for (const auto& layer : slide_info_.nonhier_layers) {
    for (const auto& record : layer.records) {
      std::string record_name =
          record.value_name.empty()
              ? absl::StrFormat("%s_%d", layer.name, record.layer_index)
              : record.value_name;

      if (record_name == name) {
        AssociatedDataInfo info;
        info.name = record_name;
        info.description = absl::StrFormat("Layer: %s", layer.name);
        info.size_bytes = 0;         // Unknown until loaded
        info.is_compressed = false;  // Unknown until loaded
        info.compression_type = "unknown";
        info.type = AssociatedDataType::kUnknown;  // Unknown until loaded

        return info;
      }
    }
  }

  return MAKE_STATUS(absl::StatusCode::kNotFound,
                     absl::StrFormat("Associated data not found: %s", name));
}

/// @brief Load associated data from disk
///
/// Reads and decodes a non-hierarchical data item. Automatically handles:
/// - Decompression (DEFLATE/zlib)
/// - Image decoding (JPEG/PNG/BMP)
/// - Type detection from magic bytes
///
/// @param name Data item name
/// @return StatusOr containing AssociatedData with decoded content
/// @retval absl::NotFoundError if data item doesn't exist or files can't be
/// opened
/// @retval absl::InternalError if reading or decoding fails
absl::StatusOr<AssociatedData> MrxsReader::LoadAssociatedData(
    std::string_view name) const {
  // Find the record
  const mrxs::NonHierarchicalRecord* target_record = nullptr;
  for (const auto& layer : slide_info_.nonhier_layers) {
    for (const auto& record : layer.records) {
      std::string record_name =
          record.value_name.empty()
              ? absl::StrFormat("%s_%d", layer.name, record.layer_index)
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
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       absl::StrFormat("Associated data not found: %s", name));
  }

  // Read the record using index reader
  std::tuple<std::string, int64_t, int64_t> record_data;
  ASSIGN_OR_RETURN(record_data, ReadNonHierRecord(target_record->record_index),
                   "Failed to read non-hierarchical record");

  auto [rel_path, offset, size] = record_data;
  fs::path datafile_path = dirname_ / rel_path;

  // Read data from file using helper
  std::vector<uint8_t> raw_data;
  ASSIGN_OR_RETURN(raw_data,
                   mrxs::MrxsDataReader::ReadData(datafile_path, offset, size),
                   "Failed to read associated data");

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
      LOG(WARNING) << "Failed to decompress data for " << name << ": "
                   << decompressed_or.status();
      decompressed_data = raw_data;
    }
  } else {
    decompressed_data = std::move(raw_data);
  }

  // Create AssociatedData
  AssociatedData result;
  result.info.name = std::string(name);
  result.info.description =
      absl::StrFormat("Layer: %s", target_record->layer_name);
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
      }

      RGBImage decoded_image;
      ASSIGN_OR_RETURN(
          decoded_image,
          mrxs::internal::DecodeImage(decompressed_data, img_format),
          "Failed to decode associated image");
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
/// @return StatusOr containing raw compressed data or error
/// @retval absl::InvalidArgumentError if file number or params are invalid
/// @retval absl::NotFoundError if data file cannot be opened
/// @retval absl::InternalError if seek or read operation fails
absl::StatusOr<std::vector<uint8_t>> MrxsReader::ReadTileData(
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
/// @return StatusOr containing TilePlan or error
/// @retval absl::InvalidArgumentError if level is invalid
/// @note May perform I/O to build spatial index on first call for a level
absl::StatusOr<core::TilePlan> MrxsReader::PrepareRequest(
    const core::TileRequest& request) const {
  // Use the plan builder helper to create the plan
  return MrxsPlanBuilder::BuildPlan(request, *this);
}

/// @brief Execute a prepared tile plan (two-stage pipeline)
///
/// Second stage of the two-stage pipeline. Executes a plan by reading tiles
/// from disk, decoding them, and writing to the output via ITileWriter.
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
absl::Status MrxsReader::ExecutePlan(const core::TilePlan& plan,
                                     runtime::TileWriter& writer) const {
  // Use the tile executor helper to execute the plan
  return MrxsTileExecutor::ExecutePlan(plan, *this, writer);
}

void MrxsReader::SetITileCache(std::shared_ptr<ITileCache> cache) {
  absl::MutexLock lock(&cache_mutex_);
  cache_ = std::move(cache);
}

std::shared_ptr<ITileCache> MrxsReader::GetITileCache() const {
  absl::MutexLock lock(&cache_mutex_);
  return cache_;
}

}  // namespace fastslide
