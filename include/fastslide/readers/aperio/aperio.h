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

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/aperio/aperio_level_info.h"
#include "fastslide/readers/aperio/metadata_parser.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

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
 * - Fast tile-based reading with JPEG decompression via simpletiff
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

/// @brief Aperio reader class implementing the SlideReader interface
class AperioReader : public TiffBasedReader,
                     public TiffReaderFactory<AperioReader> {
 public:
  /// @brief Factory method to create an AperioReader instance
  /// @param filename Path to the Aperio file
  /// @return StatusOr containing the reader instance or an error
  static aifocore::Result<std::unique_ptr<AperioReader>> Create(
      fs::path filename);

  /// @brief Destructor
  ~AperioReader() override = default;

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

  /// @brief Embedded ICC profile (TIFF tag 0x8773), if present.
  [[nodiscard]] aifocore::Result<std::vector<uint8_t>> GetIccProfile()
      const override {
    return tiff::ExtractIccProfile(GetTiffIndex());
  }

  [[nodiscard]] std::string GetFormatName() const override { return "Aperio"; }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] aifocore::Result<std::string> GetQuickHash() const override;

  // Two-stage pipeline implementation
  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

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

  /// @brief Get TIFF index for structure queries
  /// @return Const reference to TIFF index
  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return *tiff_index_;
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

  /// @brief SimpleTiff index for thread-safe TIFF operations
  std::unique_ptr<simpletiff::TiffIndex> tiff_index_;

  /// @brief Process Aperio metadata and build pyramid structure
  /// @return Status indicating success or failure
  aifocore::Status ProcessMetadata();

  /// @brief Load level/associated image information from TIFF directories
  /// @return Status indicating success or failure
  aifocore::Status LoadDirectories();

  /// @brief Populate slide properties
  void PopulateSlideProperties();
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_H_
