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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_H_

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <mutex>
#include "aifocore/status/result.h"
#include "fastslide/associated_data.h"
#include "fastslide/image.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/readers/reader_factory.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/slide_reader.h"

/**
 * @file mrxs.h
 * @brief 3DHISTECH MRXS (MIRAX) slide reader
 *
 * This header defines the MrxsReader class for reading 3DHISTECH MRXS
 * (MIRAX) whole slide images. MRXS is a directory-based format with
 * overlapping tiles and complex spatial organization.
 *
 * **Format Details:**
 * - Directory-based format with .mrxs descriptor file
 * - Metadata in Slidedat.ini (INI format)
 * - Tiles stored in separate .dat files (JPEG/PNG/BMP compressed)
 * - Overlapping tiles with subpixel positioning
 * - Camera position data for precise tile placement
 *
 * **Features:**
 * - Overlapping tile blending with automatic averaging
 * - Spatial indexing for efficient region queries
 * - Support for multiple compression formats (JPEG, PNG, BMP)
 * - Two-stage pipeline (PrepareRequest + ExecutePlan)
 * - OpenSlide-compatible quickhash computation
 * - Thread-safe tile reading with file handle pooling
 *
 * **Tile Organization:**
 * MRXS uses overlapping tiles with fractional positioning:
 * - Tiles may overlap by 10-20% for seamless blending
 * - Camera positions stored in CameraPositionX/Y files
 * - Fallback to synthetic positions if position data unavailable
 *
 * **Usage:**
 * ```cpp
 * auto reader_or = MrxsReader::Create("/path/to/file.mrxs");
 * if (!reader_or.ok()) {
 *   // Handle error
 * }
 * auto reader = std::move(*reader_or);
 *
 * // Read region with automatic tile blending
 * auto image = reader->ReadRegion({.top_left = {0, 0},
 *                                   .size = {512, 512},
 *                                   .level = 0});
 * ```
 *
 * @see SlideReader for the base interface
 * @see ReaderFactory for the CRTP factory pattern used
 * @see mrxs::MrxsSpatialIndex for the spatial indexing implementation
 */

namespace fs = std::filesystem;

namespace fastslide {

// Forward declarations
namespace mrxs {
class MrxsSpatialIndex;
}  // namespace mrxs

/// @brief MRXS (MIRAX) reader class implementing the SlideReader interface
///
/// This reader supports the MIRAX/MRXS format used by 3DHISTECH scanners.
/// MRXS files are directory-based with a main .mrxs file and associated
/// data files containing compressed tiles.
///
/// Key features:
/// - Multi-level pyramid support
/// - Overlapping tiles with automatic averaging
/// - Spatial indexing for efficient region queries
/// - JPEG/PNG/BMP tile decompression
/// - Thread-safe tile reading
class MrxsReader : public SlideReader, public ReaderFactory<MrxsReader> {
 public:
  /// @brief Factory method to create an MrxsReader instance
  /// @param filename Path to the .mrxs file
  /// @return Result containing the reader instance or an error
  static aifocore::Result<std::unique_ptr<MrxsReader>> Create(
      fs::path filename);

  /// @brief Destructor
  ~MrxsReader() override = default;

  // SlideReader interface implementation
  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;
  [[nodiscard]] const SlideProperties& GetProperties() const override;
  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;
  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override;
  [[nodiscard]] aifocore::Result<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;
  [[nodiscard]] aifocore::Result<RGBImage> ReadAssociatedImage(
      std::string_view name) const override;

  [[nodiscard]] Metadata GetMetadata() const override;

  [[nodiscard]] std::string GetFormatName() const override { return "MRXS"; }

  /// @brief Public image format used by the FFI / viewers.
  ///
  /// For brightfield slides this stays `kRGB`. Fluorescence slides with
  /// parsed filter metadata expose `kSpectral` regardless of channel
  /// count -- the underlying buffer layout for 3-channel cases is still
  /// RGB-shaped, but channels are independent fluorophores rather than
  /// color components, and downstream consumers must use the
  /// multi-channel display path. The Canvas honors this via
  /// `OutputSpec::force_spectral_image`, set by the plan builder. We
  /// also gate on `!filters.empty()` so the format stays consistent with
  /// `GetChannelMetadata()`, which returns RGB defaults when filter
  /// metadata is missing.
  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return (slide_info_.slide_type == mrxs::MrxsSlideType::kFluorescence &&
            !slide_info_.filters.empty())
               ? ImageFormat::kSpectral
               : ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    return slide_info_.camera_bitdepth >= 16 ? DataType::kUInt16
                                             : DataType::kUInt8;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] aifocore::Result<std::string> GetQuickHash() const override;

  // Two-stage pipeline implementation
  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  /// @brief Read a region with fractional positioning (MRXS-specific)
  ///
  /// This method allows fractional x/y coordinates for precise positioning
  /// with overlapping tiles. Internally converts to appropriate region spec.
  ///
  /// @param level Pyramid level (0 = full resolution)
  /// @param x X coordinate (can be fractional)
  /// @param y Y coordinate (can be fractional)
  /// @param width Width in pixels (unsigned)
  /// @param height Height in pixels (unsigned)
  /// @return Result containing the RGB image or an error
  [[nodiscard]] aifocore::Result<RGBImage> ReadRegionFractional(
      int level, double x, double y, uint32_t width, uint32_t height) const;

  /// @brief Get MRXS-specific slide information
  /// @return Reference to MRXS slide info
  [[nodiscard]] const mrxs::SlideDataInfo& GetMrxsInfo() const {
    return slide_info_;
  }

  /// @brief Get list of available associated data names.
  ///
  /// Returns *all* non-hierarchical record names known to the reader,
  /// including the `ScanDataLayer_Slide*` records that are also surfaced as
  /// associated images via @ref GetAssociatedImageNames.
  [[nodiscard]] std::vector<std::string> GetAssociatedDataNames() const;

  /// @brief Get only the non-image associated data names.
  ///
  /// Filters @ref GetAssociatedDataNames to exclude entries that are
  /// already exposed as associated images (XML / binary blobs only).
  [[nodiscard]] std::vector<std::string> GetNonImageAssociatedDataNames() const;

  /// @brief Get information about associated data without loading it
  /// @param name Data name
  /// @return Result containing data info or error
  [[nodiscard]] aifocore::Result<AssociatedDataInfo> GetAssociatedDataInfo(
      std::string_view name) const;

  /// @brief Load associated data (lazily loaded on first access)
  /// @param name Data name
  /// @return Result containing associated data or error
  [[nodiscard]] aifocore::Result<AssociatedData> LoadAssociatedData(
      std::string_view name) const;

  /// @brief Read raw tile data from data file (needed by tile executor)
  /// @param tile Tile information
  /// @return Result containing the raw data or error
  [[nodiscard]] aifocore::Result<std::vector<uint8_t>> ReadTileData(
      const mrxs::MiraxTileRecord& tile) const;

  /// @brief Get or build spatial index for a level (needed by plan builder)
  /// @param level Level index
  /// @return Result containing spatial index or error
  [[nodiscard]] aifocore::Result<std::shared_ptr<mrxs::MrxsSpatialIndex>>
  GetSpatialIndex(int level) const;

 private:
  /// @brief Allow factory access to private constructor and methods
  friend class ReaderFactory<MrxsReader>;

  /// @brief Constructor
  /// @param dirname Path to MRXS directory
  /// @param slide_info Parsed slide information
  explicit MrxsReader(fs::path dirname, mrxs::SlideDataInfo slide_info);

  /// @brief Hook 1: Validate input file (used by ReaderFactory)
  /// @param filename Path to the .mrxs file
  /// @return Status indicating validation success or failure
  static aifocore::Status ValidateInput(const fs::path& filename);

  /// @brief Hook 2: Create reader with metadata loading (used by ReaderFactory)
  /// @param filename Path to the .mrxs file
  /// @return Result containing the reader instance or error
  static aifocore::Result<std::unique_ptr<MrxsReader>> CreateReaderImpl(
      const fs::path& filename);

  /// @brief Read and parse the Slidedat.ini file
  /// @param slidedat_path Path to Slidedat.ini
  /// @param dirname Path to MRXS directory for cache keys
  /// @return Result containing SlideDataInfo or error
  static aifocore::Result<mrxs::SlideDataInfo> ReadSlidedatIni(
      const fs::path& slidedat_path, const fs::path& dirname);

  /// @brief Read a nonhier record from the index file
  /// @param record_index Record number to read
  /// @return Tuple of (datafile_path, offset, size) or error
  aifocore::Result<std::tuple<std::string, int64_t, int64_t>> ReadNonHierRecord(
      int record_index) const;

  /// @brief Read camera positions from position buffer
  /// @param dirname Directory containing MRXS files
  /// @param slide_info Slide information to update with positions
  /// @return Status indicating success or error
  static aifocore::Status ReadCameraPositions(const fs::path& dirname,
                                              mrxs::SlideDataInfo& slide_info);

  /// @brief Detect type of associated data from magic bytes
  /// @param data Raw data bytes
  /// @return Detected data type
  static AssociatedDataType DetectDataType(const std::vector<uint8_t>& data);

  /// @brief Calculate level parameters for all zoom levels
  /// @return Vector of level parameters
  std::vector<mrxs::PyramidLevelParameters> CalculateLevelParams() const;

  /// @brief Read tile information from the index file for a specific level
  /// @param level_index Zoom level index (0 = highest resolution)
  /// @return Vector of tile information or error
  aifocore::Result<std::vector<mrxs::MiraxTileRecord>> ReadLevelTiles(
      int level_index) const;

  /// @brief Decode tile data into RGB image
  /// @param data Compressed tile data
  /// @param format Image format
  /// @return Result containing decoded RGB image or error
  aifocore::Result<RGBImage> DecodeTile(const std::vector<uint8_t>& data,
                                        mrxs::MrxsImageFormat format) const;

  /// @brief Stitch tiles together with overlap averaging
  /// @param tiles Vector of tiles to stitch
  /// @param level Level index
  /// @param x X coordinate (fractional)
  /// @param y Y coordinate (fractional)
  /// @param width Width in pixels
  /// @param height Height in pixels
  /// @return Result containing stitched RGB image or error
  aifocore::Result<RGBImage> StitchTiles(
      const std::vector<mrxs::MiraxTileRecord>& tiles, int level, double x,
      double y, uint32_t width, uint32_t height) const;

  /// @brief Initialize properties from slide info
  aifocore::Status InitializeProperties();

  /// @brief Calculate slide bounds from level 0 tiles
  /// @return Result containing SlideBounds or error
  aifocore::Result<SlideBounds> CalculateBounds();

  fs::path dirname_;                ///< MRXS directory path
  mrxs::SlideDataInfo slide_info_;  ///< Slide information
  SlideProperties properties_;      ///< Standard slide properties
  std::vector<mrxs::PyramidLevelParameters>
      level_params_;  ///< Cached level parameters

  // Spatial indices (lazy-initialized, one per level)
  mutable std::vector<std::shared_ptr<mrxs::MrxsSpatialIndex>> spatial_indices_;
  mutable std::mutex spatial_index_mutex_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_H_
