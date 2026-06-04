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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/bif/bif_level_info.h"
#include "fastslide/readers/bif/bif_spatial_index.h"
#include "fastslide/readers/bif/bif_stitcher.h"
#include "fastslide/readers/bif/bif_xml.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "simpletiff/index.h"

namespace fastslide {

/// @brief Reader for Roche VENTANA BIF (DP 200) whole-slide images.
///
/// BIF is a BigTIFF variant whose high-resolution (level-0) IFD stores
/// overlapping snapshot tiles that must be re-stitched. Stitching follows the
/// normative algorithm in `docs/source/formats/bif.rst`: overlaps are binned
/// onto per grid boundary pitches (tile size minus the overlap measured at
/// that boundary). Higher pyramid levels reuse the level-0 geometry at scaled
/// overlap.
class BifReader : public TiffBasedReader, public TiffReaderFactory<BifReader> {
 public:
  static aifocore::Result<std::unique_ptr<BifReader>> Create(
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

  [[nodiscard]] std::string GetFormatName() const override { return "BIF"; }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return *tiff_index_;
  }

 private:
  friend class TiffReaderFactory<BifReader>;

  explicit BifReader(std::string_view filename);

  aifocore::Status ProcessMetadata();
  void PopulateSlideProperties();

  // Lazily builds (and caches) the spatial index for `level`.
  [[nodiscard]] aifocore::Result<const bif::BifSpatialIndex*> GetSpatialIndex(
      int level) const;

  std::unique_ptr<simpletiff::TiffIndex> tiff_index_;
  bif::ScannerInfo scanner_info_;
  bif::StitchResult stitch_;
  std::vector<BifLevelInfo> levels_;
  std::vector<BifAssociatedInfo> associated_images_;

  mutable std::mutex spatial_mutex_;
  mutable std::vector<std::shared_ptr<bif::BifSpatialIndex>> spatial_indices_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_H_
