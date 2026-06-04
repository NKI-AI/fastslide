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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/ndpitiff/ndpitiff_level_info.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "simpletiff/index.h"

namespace fastslide {

/// @brief NDPI reader (Hamamatsu) implemented on top of simpletiff.
class NdpiTiffReader : public TiffBasedReader,
                       public TiffReaderFactory<NdpiTiffReader> {
 public:
  static aifocore::Result<std::unique_ptr<NdpiTiffReader>> Create(
      std::string_view filename);

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

  [[nodiscard]] std::string GetFormatName() const override { return "NDPI"; }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  // Two-stage pipeline implementation.
  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  // Accessors for planners/executors.
  [[nodiscard]] const std::vector<NdpiTiffLevelInfo>& GetPyramidLevels() const {
    return pyramid_levels_;
  }

  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return *tiff_index_;
  }

  [[nodiscard]] uint16_t GetLevel0Page() const;

  /// @brief Build a patched NDPI JPEG header for the given tile size.
  /// @details NDPI tiles often store a "headerless" JPEG payload. This method
  ///          returns a valid JPEG header (SOI..SOS) with the provided
  ///          width/height embedded in the SOF segment(s).
  aifocore::Status BuildPatchedJpegHeader(uint16_t tile_width,
                                          uint16_t tile_height,
                                          std::vector<uint8_t>& out) const;

 private:
  friend class TiffReaderFactory<NdpiTiffReader>;

  explicit NdpiTiffReader(std::string_view filename);

  aifocore::Status ProcessMetadata();
  aifocore::Status LoadDirectories();
  void PopulateSlideProperties();
  aifocore::Status LoadJpegHeaderTemplate();

  std::vector<NdpiTiffLevelInfo> pyramid_levels_;
  std::vector<NdpiTiffAssociatedInfo> associated_images_;

  std::unique_ptr<simpletiff::TiffIndex> tiff_index_;

  // NDPI JPEG header template (SOI..SOS). Patched per tile for width/height.
  std::vector<uint8_t> jpeg_header_template_;
  std::vector<size_t> sof_height_offsets_;
  std::vector<size_t> sof_width_offsets_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_H_
