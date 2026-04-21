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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/czi/czi_spatial_index.h"
#include "fastslide/readers/reader_factory.h"
#include "fastslide/runtime/io/file_reader.h"
#include "fastslide/slide_reader.h"

namespace fastslide {

namespace fs = std::filesystem;

/// @brief Zeiss CZI slide reader.
///
/// This reader implements:
/// - Parsing the CZI file header and SubBlockDirectory (schema DV)
/// - Building pyramid levels via integer downsample factors
/// - Two-stage pipeline: `PrepareRequest()` + `ExecutePlan()`
/// - Fractional tile placement (subpixel accurate) via `BlendMetadata`
/// - Decoding JPEG-XR tiles via `jxrlib`
/// - Decompressing zstd0/zstd1 subblocks (including HiLo unpacking)
class CziReader : public SlideReader, public ReaderFactory<CziReader> {
 public:
  /// @brief Public, stable view of a parsed subblock (tile).
  ///
  /// CZI subblocks are located in the file by `file_pos` and decoded based on
  /// `compression` and `pixel_type`. Coordinates are in level-0 pixels.
  struct SubblockInfo {
    uint32_t index = 0;
    int64_t file_pos = 0;
    int32_t pixel_type = 0;
    int32_t compression = 0;
    int32_t pyramid_type = 0;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    int32_t downsample = 1;
  };

  static aifocore::Result<std::unique_ptr<CziReader>> Create(
      std::string_view filename) {
    return CreateImpl(fs::path(filename));
  }

  ~CziReader() override = default;

  int GetLevelCount() const override;

  aifocore::Result<LevelInfo> GetLevelInfo(int level) const override;

  const SlideProperties& GetProperties() const override;

  std::vector<ChannelMetadata> GetChannelMetadata() const override;

  std::vector<std::string> GetAssociatedImageNames() const override;

  aifocore::Result<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;

  aifocore::Result<Image> ReadAssociatedImage(
      std::string_view name) const override;

  Metadata GetMetadata() const override;

  std::string GetFormatName() const override { return "CZI"; }

  ImageFormat GetImageFormat() const override { return ImageFormat::kRGB; }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  ImageDimensions GetTileSize() const override;

  aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  aifocore::Status ExecutePlan(const core::TilePlan& plan,
                               runtime::Canvas& writer) const override;

  /// @brief Get the filename as string (for cache keys).
  [[nodiscard]] const std::string& GetFilename() const { return filename_; }

  /// @brief Get (or lazily build) the spatial index for a level.
  [[nodiscard]] aifocore::Result<std::shared_ptr<czi::CziSpatialIndex>>
  GetSpatialIndex(int level) const;

  /// @brief Access a parsed subblock by index.
  [[nodiscard]] SubblockInfo GetSubblockInfo(uint32_t index) const;

 private:
  friend class ReaderFactory<CziReader>;

  struct Subblock {
    uint32_t index = 0;
    int64_t file_pos = 0;  // Segment start (file-relative).
    int32_t pixel_type = 0;
    int32_t compression = 0;
    int32_t pyramid_type = 0;

    int32_t x = 0;  // Level-0 pixel coordinates after origin normalization.
    int32_t y = 0;
    uint32_t w = 0;  // Stored pixel size for this subblock.
    uint32_t h = 0;

    int32_t scene = 0;
    int32_t channel = 0;
    int32_t z_index = 0;
    int32_t downsample = 1;
  };

  struct AttachmentInfo {
    std::string name;          // e.g. "Label"
    std::string file_type;     // e.g. "JPG" or "CZI"
    int64_t file_pos = 0;      // Segment start (file-relative).
    uint32_t data_offset = 0;  // Offset from segment start to payload.
    uint32_t data_size = 0;    // Payload size in bytes (if known).
  };

  explicit CziReader(std::string filename);

  static aifocore::Status ValidateInput(const fs::path& filename);

  static aifocore::Result<std::unique_ptr<CziReader>> CreateReaderImpl(
      const fs::path& filename);

  aifocore::Status Initialize();

  // Parsing helpers
  aifocore::Status ParseFileHeader(FileReader& file);
  aifocore::Status ParseSubblockDirectory(FileReader& file);
  aifocore::Status ParseMetadataXml(FileReader& file);
  aifocore::Status ParseAttachmentDirectory(FileReader& file);
  void FinalizeDerivedState();

  // Header-derived offsets.
  int64_t subblk_dir_pos_ = 0;
  int64_t meta_pos_ = 0;
  int64_t att_dir_pos_ = 0;

  // Parsed data.
  std::string filename_;
  std::vector<Subblock> subblocks_;
  std::vector<int32_t> downsamples_;  // Sorted unique downsamples; index=level.

  // Derived slide properties and metadata.
  SlideProperties properties_{};
  std::optional<ImageDimensions> metadata_size_l0_;
  std::optional<std::pair<double, double>> metadata_mpp_;
  std::optional<double> metadata_objective_magnification_;
  std::string metadata_xml_;
  std::vector<AttachmentInfo> attachments_;

  // Full image size at level 0 (used for pyramid dimensions). This is distinct
  // from `bounds_l0_`, which is the non-empty/content bounds.
  ImageDimensions base_size_l0_{0, 0};

  // Level bounds in level-0 coordinates (content bounding box).
  SlideBounds bounds_l0_{};

  // Spatial indices per level (lazy-built, thread-safe).
  mutable std::mutex spatial_index_mutex_;
  mutable std::vector<std::shared_ptr<czi::CziSpatialIndex>> spatial_indices_;

  // Mapping from level index -> subblock indices belonging to that level.
  std::vector<std::vector<uint32_t>> level_subblocks_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_H_
