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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_H_

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/ometiff/ometiff_level_info.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "fastslide/utilities/colors.h"
#include "simpletiff/index.h"

namespace fs = std::filesystem;

namespace fastslide {

/// @brief OME channel metadata (derived from OME-XML)
struct OmeTiffChannelInfo {
  uint32_t page = 0;      ///< Page index for this channel at level 0
  std::string name;       ///< Channel name (e.g. "DAPI")
  std::string biomarker;  ///< Biomarker (often same as name)
  ColorRGB color;         ///< Display color (OME Channel@Color)
};

/// @brief Minimal slide-level metadata for OME-TIFF
struct OmeSlideMetadata {
  double mpp_x = 0.0;                  ///< PhysicalSizeX (µm/px)
  double mpp_y = 0.0;                  ///< PhysicalSizeY (µm/px)
  uint32_t z_count = 1;                ///< Focal planes (SizeZ)
  uint32_t t_count = 1;                ///< Time points (SizeT)
  std::optional<double> z_spacing_um;  ///< PhysicalSizeZ (µm between planes)
  std::optional<double> t_interval_s;  ///< TimeIncrement (seconds)
};

/// @brief OME-TIFF reader (multi-plane, pyramidal via SubIFDs)
class OmeTiffReader : public TiffBasedReader,
                      public TiffReaderFactory<OmeTiffReader> {
 public:
  static aifocore::Result<std::unique_ptr<OmeTiffReader>> Create(
      std::string_view filename);

  ~OmeTiffReader() override = default;

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

  [[nodiscard]] std::string GetFormatName() const override {
    return "OME-TIFF";
  }

  [[nodiscard]] ImageFormat GetImageFormat() const override { return format_; }

  [[nodiscard]] DataType GetDataType() const override { return data_type_; }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] StackInfo GetStackInfo() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  [[nodiscard]] const std::vector<OmeTiffLevelInfo>& GetPyramid() const {
    return pyramid_;
  }

  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return *tiff_index_;
  }

 private:
  friend class TiffReaderFactory<OmeTiffReader>;

  explicit OmeTiffReader(std::string_view filename);

  aifocore::Status ProcessMetadata();
  void PopulateSlideProperties();

  OmeSlideMetadata metadata_;
  std::vector<OmeTiffChannelInfo> channels_;
  std::vector<OmeTiffLevelInfo> pyramid_;
  std::map<std::string, uint32_t> associated_images_;

  ImageFormat format_ = ImageFormat::kSpectral;
  DataType data_type_ = DataType::kUInt8;  ///< Pixel data type
  PlanarConfig output_planar_config_ = PlanarConfig::kSeparate;

  std::unique_ptr<simpletiff::TiffIndex> tiff_index_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_H_
