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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_PHILIPSTIFF_PHILIPSTIFF_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_PHILIPSTIFF_PHILIPSTIFF_H_

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "simpletiff/index.h"

namespace fs = std::filesystem;

namespace fastslide {

/// @brief Pyramid level metadata for Philips TIFF
struct PhilipsTiffLevelInfo {
  uint16_t page = 0;               ///< TIFF page number
  ImageDimensions size = {0, 0};   ///< Level dimensions (width, height)
  double downsample_factor = 0.0;  ///< Downsample factor relative to level 0
};

/// @brief Philips TIFF reader
class PhilipsTiffReader : public TiffBasedReader,
                          public TiffReaderFactory<PhilipsTiffReader> {
 public:
  static aifocore::Result<std::unique_ptr<PhilipsTiffReader>> Create(
      const fs::path& filename);

  ~PhilipsTiffReader() override = default;

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
    return "Philips TIFF";
  }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;
  [[nodiscard]] aifocore::Result<std::string> GetQuickHash() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  [[nodiscard]] const std::vector<PhilipsTiffLevelInfo>& GetPyramidLevels()
      const {
    return pyramid_levels_;
  }

  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return *tiff_index_;
  }

 private:
  /// @brief Allow factory access to private constructor and methods
  friend class TiffReaderFactory<PhilipsTiffReader>;

  /// @brief Private constructor - use Create() factory method instead
  explicit PhilipsTiffReader(const fs::path& filename);

  /// @brief Process metadata and build pyramid structure
  aifocore::Status ProcessMetadata();

  /// @brief Populate common slide properties
  void PopulateSlideProperties();

  [[nodiscard]] aifocore::Status LoadDirectories();
  void PopulateSlidePropertiesFromXml();

  std::unique_ptr<simpletiff::TiffIndex> tiff_index_;
  std::vector<PhilipsTiffLevelInfo> pyramid_levels_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_PHILIPSTIFF_PHILIPSTIFF_H_
