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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_READER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/tiff/tiff_cache_service.h"
#include "fastslide/utilities/tiff/tiff_file.h"
#include "fastslide/utilities/tiff/tiff_pool.h"
#include "fastslide/utilities/tiff/tile_utilities.h"

/**
 * @file tiff_based_reader.h
 * @brief Base class for TIFF-based slide readers
 * 
 * This header defines TiffBasedReader, the foundation for all slide readers
 * that work with TIFF files (Aperio SVS, QPTIFF, etc.). It provides common
 * functionality including:
 * 
 * - TIFF file validation
 * - Thread-safe TIFF handle pool management
 * - Common TIFF operations (reading regions, tiles, strips)
 * - Associated image reading via libtiff
 * - Tile caching infrastructure
 * 
 * **Usage:**
 * Format-specific readers (like AperioReader, QpTiffReader) inherit from
 * this class and implement format-specific logic for:
 * - Metadata parsing
 * - Pyramid structure discovery
 * - Tile organization and addressing
 * - Channel handling
 * 
 * **Threading:**
 * The handle pool ensures thread-safe TIFF operations by providing one
 * TIFF handle per thread, preventing race conditions in libtiff.
 * 
 * **Performance:**
 * For new TIFF operations, prefer the TiffFile RAII wrapper over manual
 * TIFFGetField calls. This provides better type safety, comprehensive
 * error handling, and eliminates manual resource management.
 * 
 * @see TiffReaderFactory for the CRTP factory used to create readers
 * @see AperioReader for an Aperio SVS implementation
 * @see QpTiffReader for a PerkinElmer QPTIFF implementation
 */

namespace fs = std::filesystem;

namespace fastslide {

// Forward declaration for friend class
template <typename Derived>
class TiffReaderFactory;

/// @brief Base class for TIFF-based slide readers
///
/// This class provides common functionality for slide readers that work with
/// TIFF files, including file validation, handle pool management, and common
/// TIFF operations. It serves as a foundation for format-specific readers
/// like SVS and QPTIFF.
///
/// @note For new TIFF operations, consider using the TiffFile RAII wrapper
/// from fastslide/utilities/tiff_file.h instead of manual TIFFGetField calls.
/// This provides better type safety, comprehensive error handling, and
/// eliminates the need for manual resource management.
class TiffBasedReader : public SlideReader {
 public:
  /// @brief Allow TiffReaderFactory to access protected methods
  template <typename Derived>
  friend class TiffReaderFactory;

  /// @brief Virtual destructor
  virtual ~TiffBasedReader() = default;

  /// @brief Set tile cache for caching decoded internal tiles (old API)
  /// @param cache Shared pointer to tile cache (nullptr to disable caching)
  /// @note Deprecated: Use SetCache(std::shared_ptr<ITileCache>) instead
  void SetCache(std::shared_ptr<TileCache> cache) override;

  /// @brief Set tile cache for caching decoded internal tiles (new API)
  /// @param cache Shared pointer to tile cache interface (nullptr to disable caching)
  void SetCache(std::shared_ptr<ITileCache> cache);

 protected:
  /// @brief Constructor for derived classes
  /// @param filename Path to the TIFF file
  explicit TiffBasedReader(fs::path filename);

  /// @brief Validate that a TIFF file can be opened
  /// @param filename Path to the file
  /// @return Status indicating validity
  static absl::Status ValidateTiffFile(const fs::path& filename);

  /// @brief Initialize the thread-safe TIFF handle pool
  /// @return Status indicating success or failure
  absl::Status InitializeHandlePool();

  /// @brief Read a region from a specific TIFF page
  /// @param page Page number
  /// @param region Region specification (top_left and size)
  /// @param actual_width Output actual width
  /// @param actual_height Output actual height
  /// @param bytes_per_pixel Bytes per pixel (1 for grayscale, 3 for RGB)
  /// @return Buffer containing the region data
  std::vector<uint8_t> ReadTiffRegion(uint16_t page, const RegionSpec& region,
                                      uint32_t& actual_width,
                                      uint32_t& actual_height,
                                      uint32_t bytes_per_pixel) const;

  /// @brief Read a region from a specific TIFF page using provided TiffFile
  /// @param tiff_file TiffFile wrapper to use
  /// @param page Page number
  /// @param region Region specification (top_left and size)
  /// @param actual_width Output actual width
  /// @param actual_height Output actual height
  /// @param bytes_per_pixel Bytes per pixel (1 for grayscale, 3 for RGB)
  /// @return Buffer containing the region data
  std::vector<uint8_t> ReadTiffRegionWithFile(TiffFile& tiff_file,
                                              uint16_t page,
                                              const RegionSpec& region,
                                              uint32_t& actual_width,
                                              uint32_t& actual_height,
                                              uint32_t bytes_per_pixel) const;

  /// @brief Read an associated image from a TIFF page using TIFFReadRGBAImage
  /// @param page Page number
  /// @param width Image width
  /// @param height Image height
  /// @param name Image name for error reporting
  /// @return RGB image
  absl::StatusOr<RGBImage> ReadAssociatedImageFromPage(
      uint16_t page, uint32_t width, uint32_t height,
      const std::string& name) const;

  /// @brief Read an associated image from a TIFF page using provided TiffFile
  /// @param tiff_file TiffFile wrapper to use
  /// @param page Page number
  /// @param width Image width
  /// @param height Image height
  /// @param name Image name for error reporting
  /// @return RGB image
  absl::StatusOr<RGBImage> ReadAssociatedImageFromPageWithFile(
      TiffFile& tiff_file, uint16_t page, uint32_t width, uint32_t height,
      const std::string& name) const;

  /// @brief Get the best pyramid level for a given downsample factor
  /// @param downsample Desired downsample factor
  /// @param level_count Number of available levels
  /// @param get_level_downsample Function to get downsample for a level
  /// @return Best level index
  int GetBestLevelForDownsampleImpl(
      double downsample, int level_count,
      std::function<double(int)> get_level_downsample) const;

  /// @brief Path to the TIFF file
  fs::path filename_;

  /// @brief Slide properties
  SlideProperties properties_;

  /// @brief Thread-safe TIFF handle pool
  std::unique_ptr<TIFFHandlePool> handle_pool_;

  /// @brief TIFF cache service for handling caching operations
  std::unique_ptr<tiff::TiffCacheService> tiff_cache_service_;

 private:
  /// @brief Read a region from a tiled TIFF image
  /// @param tiff_file TIFF file wrapper
  /// @param buffer Output buffer to fill
  /// @param page Page number for cache key
  /// @param region Region specification (top_left and size)
  /// @param bytes_per_pixel Bytes per pixel
  /// @return Status indicating success or failure
  absl::Status ReadTiledRegion(TiffFile& tiff_file, uint8_t* buffer,
                               uint16_t page, const RegionSpec& region,
                               uint32_t bytes_per_pixel) const;

  /// @brief Read a region from a strip-based TIFF image
  /// @param tiff_file TIFF file wrapper
  /// @param buffer Output buffer to fill
  /// @param page Page number for cache key
  /// @param region Region specification (top_left and size)
  /// @param bytes_per_pixel Bytes per pixel
  /// @return Status indicating success or failure
  absl::Status ReadStripRegion(TiffFile& tiff_file, uint8_t* buffer,
                               uint16_t page, const RegionSpec& region,
                               uint32_t bytes_per_pixel) const;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_READER_H_
