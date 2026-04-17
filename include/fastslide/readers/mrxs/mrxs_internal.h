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

#include <cstdint>
#include <string>
#include <vector>

namespace fastslide {
namespace mrxs {

/// @brief MRXS image format enumeration
enum class MrxsImageFormat : std::uint8_t {
  kUnknown,
  kJpeg,
  kPng,
  kBmp,
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
  ///
  /// When subtiles_per_stored_image > 1, the stored image contains multiple
  /// tiles. This offset specifies which horizontal sub-region to extract. Zero
  /// when subtiles_per_stored_image = 1 (tile = entire image).
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
  /// extracted as separate sub-regions. This happens at lower zoom levels where
  /// multiple camera positions are combined into single images.
  int subtiles_per_stored_image;

  /// @brief Number of camera positions represented by each tile
  ///
  /// Typically 1 for MRXS (each tile = one camera position), but can be >1
  /// for slides without position data or with unusual camera configurations.
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
      false;  ///< True if position layer is compressed (StitchingIntensityLayer)

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
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_INTERNAL_H_
