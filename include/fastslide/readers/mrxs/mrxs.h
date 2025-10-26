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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
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
  /// @return StatusOr containing the reader instance or an error
  static absl::StatusOr<std::unique_ptr<MrxsReader>> Create(fs::path filename);

  /// @brief Destructor
  ~MrxsReader() override = default;

  // SlideReader interface implementation
  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] absl::StatusOr<LevelInfo> GetLevelInfo(
      int level) const override;
  [[nodiscard]] const SlideProperties& GetProperties() const override;
  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;
  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override;
  [[nodiscard]] absl::StatusOr<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;
  [[nodiscard]] absl::StatusOr<RGBImage> ReadAssociatedImage(
      std::string_view name) const override;

  [[nodiscard]] Metadata GetMetadata() const override;

  [[nodiscard]] std::string GetFormatName() const override { return "MRXS"; }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] absl::StatusOr<std::string> GetQuickHash() const override;

  // Two-stage pipeline implementation
  [[nodiscard]] absl::StatusOr<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] absl::Status ExecutePlan(
      const core::TilePlan& plan, runtime::TileWriter& writer) const override;

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
  /// @return StatusOr containing the RGB image or an error
  [[nodiscard]] absl::StatusOr<RGBImage> ReadRegionFractional(
      int level, double x, double y, uint32_t width, uint32_t height) const;

  /// @brief Get MRXS-specific slide information
  /// @return Reference to MRXS slide info
  [[nodiscard]] const mrxs::SlideDataInfo& GetMrxsInfo() const {
    return slide_info_;
  }

  /// @brief Get list of available associated data names
  /// @return Vector of data names
  [[nodiscard]] std::vector<std::string> GetAssociatedDataNames() const;

  /// @brief Get information about associated data without loading it
  /// @param name Data name
  /// @return StatusOr containing data info or error
  [[nodiscard]] absl::StatusOr<AssociatedDataInfo> GetAssociatedDataInfo(
      std::string_view name) const;

  /// @brief Load associated data (lazily loaded on first access)
  /// @param name Data name
  /// @return StatusOr containing associated data or error
  [[nodiscard]] absl::StatusOr<AssociatedData> LoadAssociatedData(
      std::string_view name) const;

  /// @brief Read raw tile data from data file (needed by tile executor)
  /// @param tile Tile information
  /// @return StatusOr containing the raw data or error
  [[nodiscard]] absl::StatusOr<std::vector<uint8_t>> ReadTileData(
      const mrxs::MiraxTileRecord& tile) const;

  /// @brief Get or build spatial index for a level (needed by plan builder)
  /// @param level Level index
  /// @return StatusOr containing spatial index or error
  [[nodiscard]] absl::StatusOr<std::shared_ptr<mrxs::MrxsSpatialIndex>>
  GetSpatialIndex(int level) const;

  /// @brief Set tile cache for this reader (ITileCache interface)
  ///
  /// Injects a tile cache that will be used to cache decoded camera images.
  /// This significantly improves performance for repeated access to the same
  /// regions.
  ///
  /// @param cache Shared pointer to cache implementation
  void SetITileCache(std::shared_ptr<ITileCache> cache);

  /// @brief Get the current tile cache (ITileCache interface)
  /// @return Shared pointer to cache (may be nullptr)
  [[nodiscard]] std::shared_ptr<ITileCache> GetITileCache() const;

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
  static absl::Status ValidateInput(const fs::path& filename);

  /// @brief Hook 2: Create reader with metadata loading (used by ReaderFactory)
  /// @param filename Path to the .mrxs file
  /// @return StatusOr containing the reader instance or error
  static absl::StatusOr<std::unique_ptr<MrxsReader>> CreateReaderImpl(
      const fs::path& filename);

  /// @brief Read and parse the Slidedat.ini file
  /// @param slidedat_path Path to Slidedat.ini
  /// @param dirname Path to MRXS directory for cache keys
  /// @return StatusOr containing SlideDataInfo or error
  static absl::StatusOr<mrxs::SlideDataInfo> ReadSlidedatIni(
      const fs::path& slidedat_path, const fs::path& dirname);

  /// @brief Read a nonhier record from the index file
  /// @param record_index Record number to read
  /// @return Tuple of (datafile_path, offset, size) or error
  absl::StatusOr<std::tuple<std::string, int64_t, int64_t>> ReadNonHierRecord(
      int record_index) const;

  /// @brief Read camera positions from position buffer
  /// @param dirname Directory containing MRXS files
  /// @param slide_info Slide information to update with positions
  /// @return Status indicating success or error
  static absl::Status ReadCameraPositions(const fs::path& dirname,
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
  absl::StatusOr<std::vector<mrxs::MiraxTileRecord>> ReadLevelTiles(
      int level_index) const;

  /// @brief Decode tile data into RGB image
  /// @param data Compressed tile data
  /// @param format Image format
  /// @return StatusOr containing decoded RGB image or error
  absl::StatusOr<RGBImage> DecodeTile(const std::vector<uint8_t>& data,
                                      mrxs::MrxsImageFormat format) const;

  /// @brief Stitch tiles together with overlap averaging
  /// @param tiles Vector of tiles to stitch
  /// @param level Level index
  /// @param x X coordinate (fractional)
  /// @param y Y coordinate (fractional)
  /// @param width Width in pixels
  /// @param height Height in pixels
  /// @return StatusOr containing stitched RGB image or error
  absl::StatusOr<RGBImage> StitchTiles(
      const std::vector<mrxs::MiraxTileRecord>& tiles, int level, double x,
      double y, uint32_t width, uint32_t height) const;

  /// @brief Initialize properties from slide info
  absl::Status InitializeProperties();

  /// @brief Calculate slide bounds from level 0 tiles
  /// @return StatusOr containing SlideBounds or error
  absl::StatusOr<SlideBounds> CalculateBounds();

  fs::path dirname_;                ///< MRXS directory path
  mrxs::SlideDataInfo slide_info_;  ///< Slide information
  SlideProperties properties_;      ///< Standard slide properties
  std::vector<mrxs::PyramidLevelParameters>
      level_params_;  ///< Cached level parameters

  // Spatial indices (lazy-initialized, one per level)
  mutable std::vector<std::shared_ptr<mrxs::MrxsSpatialIndex>> spatial_indices_;
  mutable absl::Mutex spatial_index_mutex_;

  // Cache for decoded camera images
  std::shared_ptr<ITileCache> cache_;
  mutable absl::Mutex cache_mutex_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_H_
