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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/omezarr/omezarr_level_info.h"
#include "fastslide/readers/omezarr/omezarr_metadata.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/colors.h"

namespace fastslide {

/// @brief OME-Zarr v0.4 / v0.5 (Zarr V3) pyramidal reader.
///
/// Supports the common OME-NGFF axis layouts: "yx", "cyx", "zyx", "tyx",
/// "tczyx" (with `t`/`z` axes restricted to size 1). Channels are read as
/// separate planes; the canvas paints them into individual channel slots.
class OmeZarrReader : public SlideReader {
 public:
  static aifocore::Result<std::unique_ptr<OmeZarrReader>> Create(
      std::string_view path);

  ~OmeZarrReader() override = default;

  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;

  [[nodiscard]] const SlideProperties& GetProperties() const override {
    return properties_;
  }

  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;

  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override {
    return {};
  }

  [[nodiscard]] aifocore::Result<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;
  [[nodiscard]] aifocore::Result<RGBImage> ReadAssociatedImage(
      std::string_view name) const override;

  [[nodiscard]] Metadata GetMetadata() const override;

  [[nodiscard]] std::string GetFormatName() const override {
    return "OME-ZARR";
  }

  [[nodiscard]] ImageFormat GetImageFormat() const override { return format_; }

  [[nodiscard]] DataType GetDataType() const override { return data_type_; }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& canvas) const override;

  [[nodiscard]] const std::vector<OmeZarrLevelInfo>& GetPyramid() const {
    return pyramid_;
  }

  [[nodiscard]] const std::string& GetFilename() const { return filename_; }

 private:
  explicit OmeZarrReader(std::string path);

  aifocore::Status LoadMetadata();
  void PopulateSlideProperties();

  std::string filename_;
  std::filesystem::path root_dir_;
  formats::omezarr::OmeNgffMetadata ngff_;
  std::vector<OmeZarrLevelInfo> pyramid_;
  std::vector<ChannelMetadata> channels_;
  SlideProperties properties_;

  ImageFormat format_ = ImageFormat::kSpectral;
  DataType data_type_ = DataType::kUInt8;
  PlanarConfig planar_config_ = PlanarConfig::kSeparate;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_H_
