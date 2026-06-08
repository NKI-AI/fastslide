// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_H_

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/imagejtiff/imagejtiff_level_info.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "simpletiff/index.h"

namespace fs = std::filesystem;

namespace fastslide {

/// @brief Reader for ImageJ / Fiji hyperstack TIFFs.
///
/// ImageJ writes multi-channel (and z/t) stacks as a sequence of single-sample
/// TIFF pages, one per (channel, z, t) plane, all at a single resolution. The
/// channel count lives in the page-0 `ImageDescription` (`channels=N`). This
/// is structurally identical to QPTIFF's page-per-channel model, so the shared
/// multi-channel plan builder and tile executor are reused.
///
/// The data type is inferred via `DataTypeFromSampleFormat`, which follows the
/// TIFF spec (and tifffile): SampleFormat == 3 is float, while an absent or
/// unsigned tag is an unsigned integer - so a 32-bit ImageJ page without the
/// tag is read as uint32, not float.
class ImageJTiffReader : public TiffBasedReader,
                         public TiffReaderFactory<ImageJTiffReader> {
 public:
  /// @brief Factory method to create an ImageJTiffReader instance.
  static aifocore::Result<std::unique_ptr<ImageJTiffReader>> Create(
      const fs::path& filename);

  ~ImageJTiffReader() override = default;

  // SlideReader interface implementation.
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
    return "ImageJ TIFF";
  }

  [[nodiscard]] ImageFormat GetImageFormat() const override { return format_; }

  [[nodiscard]] DataType GetDataType() const override { return data_type_; }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  // Two-stage pipeline implementation.
  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  /// @brief Get pyramid levels (single level for ImageJ hyperstacks).
  [[nodiscard]] const std::vector<ImageJTiffLevelInfo>& GetPyramid() const {
    return pyramid_;
  }

  /// @brief Get TIFF index for structure queries.
  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return *tiff_index_;
  }

  /// @brief Detect whether a TIFF file is an ImageJ TIFF by inspecting the
  ///        page-0 ImageDescription. Used by the shared .tif factory to route
  ///        between readers without opening the file twice on the hot path.
  [[nodiscard]] static bool IsImageJTiff(const simpletiff::TiffIndex& index);

 private:
  friend class TiffReaderFactory<ImageJTiffReader>;

  explicit ImageJTiffReader(const fs::path& filename);

  /// @brief Parse the ImageDescription, build the channel pages and infer the
  ///        image format / data type.
  aifocore::Status ProcessMetadata();

  void PopulateSlideProperties();

  std::unique_ptr<simpletiff::TiffIndex> tiff_index_;
  std::vector<ImageJTiffLevelInfo> pyramid_;
  std::vector<ChannelMetadata> channels_;
  PlanarConfig output_planar_config_ = PlanarConfig::kSeparate;
  ImageFormat format_ = ImageFormat::kSpectral;
  DataType data_type_ = DataType::kUInt8;
  uint32_t num_channels_ = 1;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_H_
