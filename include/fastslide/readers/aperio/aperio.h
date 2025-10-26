// Copyright 2024 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_H_

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fastslide/image.h"
#include "fastslide/readers/aperio/aperio_plan_builder.h"
#include "fastslide/readers/aperio/metadata_parser.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"

/**
 * @file aperio.h
 * @brief Aperio SVS slide reader
 *
 * This header defines the AperioReader class for reading Aperio SVS
 * (ScanScope Virtual Slide) whole slide images. Aperio SVS is a widely-used
 * format for brightfield histology images.
 *
 * **Format Details:**
 * - Based on BigTIFF with JPEG-compressed tiles
 * - Metadata stored in ImageDescription TIFF tag
 * - Pyramid levels organized by decreasing resolution
 * - Associated images (thumbnail, macro, label) stored as TIFF directories
 *
 * **Features:**
 * - Fast tile-based reading with JPEG decompression via libtiff
 * - OpenSlide-compatible quickhash computation
 * - Two-stage pipeline (PrepareRequest + ExecutePlan)
 * - Support for both tiled and stripped TIFF formats
 * - Efficient region extraction with minimal memory overhead
 *
 * **Usage:**
 * ```cpp
 * auto reader_or = AperioReader::Create("/path/to/file.svs");
 * if (!reader_or.ok()) {
 *   // Handle error
 * }
 * auto reader = std::move(*reader_or);
 * auto image = reader->ReadRegion({.top_left = {0, 0},
 *                                   .size = {512, 512},
 *                                   .level = 0});
 * ```
 *
 * @see TiffBasedReader for the base class
 * @see TiffReaderFactory for the CRTP factory pattern used
 */

namespace fs = std::filesystem;

namespace fastslide {

// Forward declarations
class TIFFHandlePool;

/// @brief Pyramid level metadata for Aperio
struct AperioLevelInfo {
  uint16_t page = 0;               ///< TIFF page number
  ImageDimensions size = {0, 0};   ///< Level dimensions (width, height)
  double downsample_factor = 0.0;  ///< Downsample factor relative to level 0
};

/// @brief Associated image metadata for Aperio
struct AperioAssociatedInfo {
  uint16_t page;                  ///< TIFF page number
  ImageDimensions size = {0, 0};  ///< Image dimensions (width, height)
  std::string name;               ///< Image name (e.g., "thumbnail", "macro")
};

/// @brief Aperio reader class implementing the SlideReader interface
class AperioReader : public TiffBasedReader,
                     public TiffReaderFactory<AperioReader> {
 public:
  /// @brief Factory method to create an AperioReader instance
  /// @param filename Path to the Aperio file
  /// @return StatusOr containing the reader instance or an error
  static absl::StatusOr<std::unique_ptr<AperioReader>> Create(
      fs::path filename);

  /// @brief Destructor
  ~AperioReader() override = default;

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

  [[nodiscard]] std::string GetFormatName() const override { return "Aperio"; }

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

  /// @brief Get Aperio metadata (format-specific)
  /// @return Reference to Aperio metadata
  [[nodiscard]] const formats::aperio::AperioMetadata& GetAperioMetadata()
      const {
    return aperio_metadata_;
  }

  /// @brief Get pyramid levels (format-specific)
  /// @return Reference to pyramid levels
  [[nodiscard]] const std::vector<AperioLevelInfo>& GetPyramidLevels() const {
    return pyramid_levels_;
  }

  /// @brief Get associated images (format-specific)
  /// @return Reference to associated images
  [[nodiscard]] const std::vector<AperioAssociatedInfo>& GetAssociatedImages()
      const {
    return associated_images_;
  }

  /// @brief Get handle pool for TIFF operations
  /// @return Raw pointer to handle pool
  [[nodiscard]] TIFFHandlePool* GetHandlePool() const {
    return handle_pool_.get();
  }

 private:
  /// @brief Allow factory access to private constructor and methods
  friend class TiffReaderFactory<AperioReader>;

  /// @brief Private constructor - use Create() factory method instead
  /// @param filename Path to the Aperio file
  explicit AperioReader(fs::path filename);

  formats::aperio::AperioMetadata
      aperio_metadata_;                          ///< Aperio-specific metadata
  std::vector<AperioLevelInfo> pyramid_levels_;  ///< Pyramid levels
  std::vector<AperioAssociatedInfo> associated_images_;  ///< Associated images

  /// @brief Cached TIFF structure metadata from most recent PrepareRequest call
  /// @note Mutable to allow caching in const PrepareRequest method
  mutable TiffStructureMetadata tiff_metadata_;

  /// @brief Process Aperio metadata and build pyramid structure
  /// @return Status indicating success or failure
  absl::Status ProcessMetadata();

  /// @brief Load level/associated image information from TIFF directories
  /// @return Status indicating success or failure
  absl::Status LoadDirectories();

  /// @brief Populate slide properties
  void PopulateSlideProperties();
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_H_
