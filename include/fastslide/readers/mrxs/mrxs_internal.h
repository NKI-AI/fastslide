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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INTERNAL_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INTERNAL_H_

/// @file mrxs_internal.h
/// @brief Internal data structures for MRXS reader
///
/// DERIVED FROM: Python miraxreader (MIT License)
/// Reference: https://github.com/rharkes/miraxreader
///
/// These C++ structures are adapted from Python miraxreader's design.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fastslide {
namespace mrxs {

namespace internal {

/// @brief Prefix used by 3DHISTECH for non-hierarchical records that hold
///        full RGB images (label, macro, thumbnails, …).
///
/// `MrxsReader` surfaces those records through the standard
/// `SlideReader::GetAssociatedImageNames` / `ReadAssociatedImage` API, with
/// this prefix stripped from the public name. The same prefix is used to
/// filter the records out of `GetAssociatedDataNames` so that
/// non-image associated data (XML, binary blobs) and image associated data
/// stay in distinct namespaces from a Python perspective.
inline constexpr std::string_view kAssociatedImagePrefix =
    "ScanDataLayer_Slide";

}  // namespace internal

/// @brief MRXS image format enumeration
enum class MrxsImageFormat : std::uint8_t {
  kUnknown,
  kJpeg,
  kPng,
  kBmp,
  kJpegXr,  ///< JPEG-XR (used by 3DHISTECH fluorescence slides, 8/16-bit)
};

/// @brief High-level MRXS slide modality
///
/// Driven by the `SLIDE_TYPE` value in the `[GENERAL]` section of
/// `Slidedat.ini`. Brightfield is the default; fluorescence triggers
/// per-channel metadata parsing and 16-bit image handling.
enum class MrxsSlideType : std::uint8_t {
  kBrightfield,
  kFluorescence,
};

/// @brief Fluorescence filter channel metadata
///
/// Parsed from `LAYER_<filter_hier>_LEVEL_<index>_SECTION` entries in
/// `Slidedat.ini`. For 3DHISTECH fluorescence slides, multiple filters are
/// typically packed into the R/G/B planes of a single 16-bit RGB tile, with
/// `storing_channel` selecting the plane (R=0, G=1, B=2).
struct FilterChannel {
  int index = 0;                     ///< Filter index within the hierarchy
  std::string name;                  ///< Human-readable filter name
  double excitation_wavelength = 0;  ///< Peak excitation wavelength (nm)
  double emission_wavelength = 0;    ///< Peak emission wavelength (nm)
  std::array<uint8_t, 3> color_rgb{0, 0, 0};  ///< Display color
  int storing_channel = 0;        ///< RGB plane index storing this channel
  std::string data_filter_level;  ///< e.g. "FilterLevel_0"
  int exposure_time_us = 0;       ///< Exposure time in microseconds
  int digital_gain = 0;           ///< Digital gain setting
  bool is_master = false;         ///< Master filter flag
  bool is_stitching = false;      ///< Stitching filter flag
};

/// @brief Tile record metadata from MRXS index file
///
/// DERIVED FROM: Python miraxreader tile data structures (MIT License)
/// Reference: https://github.com/rharkes/miraxreader
///
/// This structure corresponds to Python's tile metadata, adapted for C++17
/// with strong typing and validation. Each record contains all information
/// needed to locate and decode a tile from the MRXS data files.
///
/// Each tile represents a rectangular region of the slide at a specific zoom
/// level. Tiles may be complete stored images (subtiles_per_stored_image=1) or
/// sub-regions extracted from larger stored images
/// (subtiles_per_stored_image>1).
struct MiraxTileRecord {
  /// @brief Linear index of the source image in the level
  ///
  /// Images are indexed linearly: image_index = y * images_across + x
  /// This is the image as stored in the index file, before subdivision.
  int32_t image_index = 0;

  /// @brief Byte offset of compressed image data within the data file
  int32_t offset = 0;

  /// @brief Size in bytes of the compressed image data (JPEG/PNG/BMP)
  int32_t length = 0;

  /// @brief Index of the data file containing this tile (0-based)
  ///
  /// MRXS slides store data across multiple files (Dat_0.dat, Dat_1.dat, etc.)
  int32_t data_file_number = 0;

  /// @brief Tile X coordinate in the level's tile grid
  ///
  /// This is the logical tile position after accounting for
  /// camera_image_divisions and tile subdivision. May differ from image_index %
  /// images_across.
  int32_t x = 0;

  /// @brief Tile Y coordinate in the level's tile grid
  int32_t y = 0;

  /// @brief X offset (in pixels) for extracting this tile from the source image
  double subregion_x = 0.0;

  /// @brief Y offset (in pixels) for extracting this tile from the source image
  double subregion_y = 0.0;

  /// @brief Intensity gain correction value for this tile's camera position
  ///
  /// Value is typically close to 1.0 (range ~0.97-1.04). Used for intensity
  /// normalization to correct scanner illumination variations across camera
  /// positions. A value of 1.0 means no adjustment needed.
  /// Applied as: linear_output = linear_input * gain (in linear RGB space)
  /// Available in MRXS slides version ≥ 2.2.
  float gain = 1.0f;

  /// @brief Which group of up to 3 channels this stored image carries.
  ///
  /// Fluorescence MRXS slides pack at most three filters into the R/G/B
  /// planes of a single PNG. Slides with more than three channels store
  /// additional channel-groups as separate PNGs that share the same
  /// `image_index` (Java `nextCounter`) in the index file. `0` means the
  /// PNG carries channels [0..min(3, N)-1]; `1` means [3..min(6, N)-1] and
  /// so on. For 8-bit RGB brightfield slides this is always `0`.
  int32_t channel_group_index = 0;
};

/// @brief Slide zoom level information (pyramid level metadata)
///
/// DERIVED FROM: Python miraxreader SlideZoomLevel or similar (MIT License)
/// Reference: https://github.com/rharkes/miraxreader
///
/// This structure stores metadata for one level of the multi-resolution
/// pyramid, following Python miraxreader's descriptive naming conventions.
struct SlideZoomLevel {
  int downsample_exponent;        ///< Downsampling exponent for pyramid level
  double x_overlap_pixels;        ///< X overlap in pixels
  double y_overlap_pixels;        ///< Y overlap in pixels
  double mpp_x;                   ///< Microns per pixel in X
  double mpp_y;                   ///< Microns per pixel in Y
  uint32_t background_color_rgb;  ///< Background fill color (RGB)
  MrxsImageFormat image_format;   ///< Image format (JPEG/PNG/BMP)
  int image_width;                ///< Tile width in pixels
  int image_height;               ///< Tile height in pixels
  std::string section_name;       ///< Section name from INI file
};

/// @brief Pyramid level parameters for tile layout and positioning
///
/// DERIVED FROM: Python miraxreader level calculation logic (MIT License)
/// Reference: https://github.com/rharkes/miraxreader
///
/// MRXS files use a complex multi-resolution structure where lower resolution
/// levels are created by downsampling and concatenating images. These
/// parameters describe how tiles are organized at each zoom level. Python-style
/// verbose naming for clarity.
struct PyramidLevelParameters {
  /// @brief Concatenation factor = 2^(sum of concat_exponents up to this level)
  ///
  /// Represents how many base-level images have been concatenated in each
  /// dimension. Example: concatenation_factor=4 means 4x4=16 base images are
  /// combined into one stored image.
  int concatenation_factor;

  /// @brief Divisor for tile count calculations
  ///
  /// Determines whether reducing resolution decreases tile count or tile size.
  /// Equals min(concatenation_factor, camera_image_divisions). This "bottoms
  /// out" at camera_image_divisions to prevent excessive tile subdivision.
  int grid_divisor;

  /// @brief Number of logical tiles contained in each stored image
  ///
  /// When > 1, each stored JPEG/PNG/BMP contains multiple tiles that must be
  /// extracted as separate sub-regions. This value is > 1 only when the slide
  /// has position data or overlapping tiles.
  /// When the slide has no overlaps and no position data, this is 1.
  int subtiles_per_stored_image;

  /// @brief Number of camera positions represented by each tile
  int camera_positions_per_tile;

  /// @brief Horizontal spacing (in pixels) between tile centers
  ///
  /// Accounts for overlap between adjacent camera positions. Tiles are placed
  /// at (horizontal_tile_step * col, vertical_tile_step * row) in the
  /// coordinate space.
  double horizontal_tile_step;

  /// @brief Vertical spacing (in pixels) between tile centers
  double vertical_tile_step;
};

/// @brief Non-hierarchical record information (associated data metadata)
///
/// DERIVED FROM: Python miraxreader non-hierarchical record handling (MIT
/// License) Reference: https://github.com/rharkes/miraxreader
///
/// Full name "NonHierarchical" rather than abbreviated "NonHier" follows
/// Python's preference for explicit, self-documenting names.
struct NonHierarchicalRecord {
  std::string layer_name;  ///< Parent layer name
  std::string value_name;  ///< Value name (e.g., "ScanDataLayer_SlidePreview")
  std::string section_name;  ///< Section name from INI
  int record_index;          ///< Record index in nonhier list
  int layer_index;           ///< Index within layer
};

/// @brief Non-hierarchical layer information (group of associated records)
///
/// DERIVED FROM: Python miraxreader non-hierarchical layer handling (MIT
/// License) Reference: https://github.com/rharkes/miraxreader
///
/// Full name "NonHierarchical" follows Python's explicit naming style.
struct NonHierarchicalLayer {
  std::string name;   ///< Layer name
  int count;          ///< Number of records
  int record_offset;  ///< Offset in nonhier record list
  std::vector<NonHierarchicalRecord> records;  ///< Individual records
};

/// @brief Slide data information - main slide metadata container
///
/// DERIVED FROM: Python miraxreader SlideData class (MIT License)
/// Reference: https://github.com/rharkes/miraxreader
///
/// This structure corresponds to Python's SlideData class.
struct SlideDataInfo {
  std::string slide_id;                     ///< Slide ID
  std::string dirname;                      ///< Directory name (for cache keys)
  int images_x;                             ///< Number of images in X direction
  int images_y;                             ///< Number of images in Y direction
  int objective_magnification;              ///< Objective magnification
  int image_divisions;                      ///< Image divisions
  std::vector<std::string> datafile_paths;  ///< Paths to data files
  std::vector<SlideZoomLevel> zoom_levels;  ///< Zoom level information
  std::string index_filename;               ///< Index filename

  // Camera position data (2 values per position: x, y)
  // Empty if using synthetic positions
  std::vector<int32_t> camera_positions;  ///< Camera positions from file
  bool using_synthetic_positions = true;  ///< True if using synthetic positions

  // Position layer metadata (detected during INI parsing)
  std::string position_layer_name;  ///< Name of position layer if found
                                    ///< (VIMSLIDE_POSITION_BUFFER or
                                    ///< StitchingIntensityLayer)
  int position_layer_record_offset =
      -1;  ///< Record offset for position data (-1 if not found)
  bool position_layer_compressed =
      false;  ///< True if position layer is compressed
              ///< (StitchingIntensityLayer)

  // Camera position intensity gain values (1 value per position)
  // Values typically range 0.97-1.04, centered around 1.0
  // Used for intensity normalization to correct illumination variations
  // Applied as linear multipliers in linear RGB space
  // Empty if not available (MRXS version < 2.2)
  std::vector<float>
      camera_position_gains;  ///< Intensity gain values per camera position

  // Non-hierarchical layers (associated images, XML, binary data)
  std::vector<NonHierarchicalLayer>
      nonhier_layers;  ///< Non-hierarchical layer metadata

  // Slide modality and pixel bit depth.
  MrxsSlideType slide_type = MrxsSlideType::kBrightfield;
  std::string slide_type_raw;  ///< Verbatim SLIDE_TYPE string
  int camera_bitdepth = 8;     ///< VIMSLIDE_SLIDE_BITDEPTH (8 or 16)

  // Fluorescence filter channels (empty for brightfield slides).
  std::vector<FilterChannel> filters;

  /// @brief Per-hierarchy entry count from `[HIERARCHICAL]` (`HIER_<i>_COUNT`).
  ///
  /// Kept for diagnostic purposes; the on-disk hierarchical root pointer
  /// table actually allocates `pyramidDepth` slots per hierarchy
  /// (regardless of `HIER_<i>_COUNT`), so the index reader uses
  /// `pyramidDepth * nHierarchies` as the slot count instead.
  std::vector<int32_t> hier_counts;

  /// @brief Total number of hierarchies declared in `[HIERARCHICAL]`.
  ///
  /// Equal to `HIER_COUNT`. Used by the index reader to know how many
  /// per-pyramid-level hierarchy slots to walk in the hier_root pointer
  /// table.
  int32_t nhierarchies = 0;

  /// @brief Index of the `Slide filter level` hierarchy (-1 if none).
  ///
  /// 3DHISTECH stores up to 3 fluorophores in the R/G/B planes of each
  /// stored RGB tile; channel groups beyond the first are stored in
  /// additional hierarchies (notably `Slide filter level`). The index
  /// reader walks every hierarchy at the requested pyramid level and uses
  /// MiraxReader's "skip leading metadata" heuristic to route the right
  /// records to each channel group, so this index is informational only.
  int32_t filter_hier_index = -1;
};

/// @brief Unpack 24-bit 0xRRGGBB (`SlideZoomLevel::background_color_rgb`).
[[nodiscard]] constexpr std::array<uint8_t, 3> UnpackRgb24(uint32_t rgb) {
  return {static_cast<uint8_t>((rgb >> 16) & 0xFF),
          static_cast<uint8_t>((rgb >> 8) & 0xFF),
          static_cast<uint8_t>(rgb & 0xFF)};
}

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INTERNAL_H_
