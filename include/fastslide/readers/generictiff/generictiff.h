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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_H_

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "simpletiff/index.h"

namespace fs = std::filesystem;

namespace fastslide {

/// @brief Pyramid level metadata for Generic TIFF
struct GenericTiffLevelInfo {
  uint16_t page = 0;               ///< TIFF page number
  ImageDimensions size = {0, 0};   ///< Level dimensions (width, height)
  double downsample_factor = 0.0;  ///< Downsample factor relative to level 0
};

/// @brief Associated image metadata for Generic TIFF
struct GenericTiffAssociatedInfo {
  uint16_t page;                  ///< TIFF page number
  ImageDimensions size = {0, 0};  ///< Image dimensions (width, height)
  std::string name;               ///< Image name (e.g., "thumbnail", "macro")
};

/// @brief Generic TIFF reader class implementing the SlideReader interface
class GenericTiffReader : public TiffBasedReader,
                          public TiffReaderFactory<GenericTiffReader> {
 public:
  /// @brief Factory method to create a GenericTiffReader instance
  /// @param filename Path to the TIFF file
  /// @return StatusOr containing the reader instance or an error
  static aifocore::Result<std::unique_ptr<GenericTiffReader>> Create(
      const fs::path& filename);

  /// @brief Destructor
  ~GenericTiffReader() override = default;

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

  [[nodiscard]] std::string GetFormatName() const override {
    return "Generic TIFF";
  }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    if (!tiff_index_ || pyramid_levels_.empty()) {
      return ImageFormat::kRGB;
    }
    const uint16_t page = pyramid_levels_[0].page;
    if (page >= tiff_index_->NumPages()) {
      return ImageFormat::kRGB;
    }
    const auto& page_header = tiff_index_->Page(page);
    if (page_header.samples_per_pixel == 1) {
      return ImageFormat::kGray;
    }
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    if (!tiff_index_ || pyramid_levels_.empty()) {
      return DataType::kUInt8;
    }
    const uint16_t page = pyramid_levels_[0].page;
    if (page >= tiff_index_->NumPages()) {
      return DataType::kUInt8;
    }
    return DataTypeFromBitsPerSample(tiff_index_->Page(page).bits_per_sample);
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] aifocore::Result<std::string> GetQuickHash() const override;

  // Two-stage pipeline implementation
  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  /// @brief Get pyramid levels
  /// @return Reference to pyramid levels
  [[nodiscard]] const std::vector<GenericTiffLevelInfo>& GetPyramidLevels()
      const {
    return pyramid_levels_;
  }

  /// @brief Get index of main level 0 page
  /// @return Page index
  [[nodiscard]] uint16_t GetLevel0Page() const;

  /// @brief Get TIFF index for structure queries
  /// @return Const reference to TIFF index
  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return *tiff_index_;
  }

 private:
  /// @brief Allow factory access to private constructor and methods
  friend class TiffReaderFactory<GenericTiffReader>;

  /// @brief Private constructor - use Create() factory method instead
  explicit GenericTiffReader(const fs::path& filename);

  std::vector<GenericTiffLevelInfo> pyramid_levels_;  ///< Pyramid levels
  std::vector<GenericTiffAssociatedInfo>
      associated_images_;  ///< Associated images (if any)

  /// @brief SimpleTiff index for thread-safe TIFF operations
  std::unique_ptr<simpletiff::TiffIndex> tiff_index_;

  /// @brief Process metadata and build pyramid structure
  /// @return Status indicating success or failure
  aifocore::Status ProcessMetadata();

  /// @brief Load level/associated image information from TIFF directories
  /// @return Status indicating success or failure
  aifocore::Status LoadDirectories();

  /// @brief Populate slide properties
  void PopulateSlideProperties();
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_H_
