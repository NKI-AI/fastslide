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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/czi/czi_level_info.h"
#include "fastslide/readers/czi/czi_scene_image.h"
#include "fastslide/readers/reader_factory.h"
#include "fastslide/runtime/io/file_reader.h"
#include "fastslide/slide_reader.h"

namespace pugi {
class xml_node;
}  // namespace pugi

namespace fastslide {

namespace fs = std::filesystem;

/// @brief Zeiss CZI slide reader (multi-image container).
///
/// Implemented from the public Zeiss CZI / ZISRAW binary-format definition.
/// This reader:
/// - Parses the CZI file header and SubBlockDirectory (schema "DV").
/// - Surfaces each CZI scene (the "S" dimension) as its own `SlideImage`
///   (`CziSceneImage`); the legacy single-image surface forwards to the
///   primary scene (the one with the lowest scene index).
/// - Builds pyramid levels per scene via integer downsample factors.
/// - Decodes uncompressed BGR, JPEG, JPEG-XR and zstd0/zstd1 subblocks
///   (including the optional 16-bit lo-hi byte unpacking).
class CziReader : public SlideReader, public ReaderFactory<CziReader> {
 public:
  /// @brief Public, stable view of a parsed subblock (tile).
  using SubblockInfo = CziSubblockInfo;

  static aifocore::Result<std::unique_ptr<CziReader>> Create(
      std::string_view filename) {
    return CreateImpl(fs::path(filename));
  }

  ~CziReader() override;

  // -- Multi-image container API ----------------------------------------
  [[nodiscard]] int GetImageCount() const override {
    return static_cast<int>(images_.size());
  }

  [[nodiscard]] int GetPrimaryImageIndex() const override {
    return primary_index_;
  }

  [[nodiscard]] std::vector<std::string> GetImageNames() const override;
  [[nodiscard]] aifocore::Result<const SlideImage*> GetImage(
      int index) const override;

  // -- Primary-scene forwarders (single-image surface) ------------------
  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;
  [[nodiscard]] const SlideProperties& GetProperties() const override;
  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;
  [[nodiscard]] ImageDimensions GetTileSize() const override;
  [[nodiscard]] StackInfo GetStackInfo() const override;
  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  // -- Associated images (container level) ------------------------------
  //
  // Associated images are surfaced under their native CZI attachment names
  // (e.g. "Label", "SlidePreview", "Thumbnail") with no renaming.
  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override;
  [[nodiscard]] aifocore::Result<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;
  [[nodiscard]] aifocore::Result<Image> ReadAssociatedImage(
      std::string_view name) const override;

  [[nodiscard]] Metadata GetMetadata() const override;

  [[nodiscard]] std::string GetFormatName() const override { return "CZI"; }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return Primary().GetImageFormat();
  }

  [[nodiscard]] DataType GetDataType() const override {
    return Primary().GetDataType();
  }

  /// @brief Get the filename as string (for cache keys).
  [[nodiscard]] const std::string& GetFilename() const { return filename_; }

  /// @brief Shared view of all parsed subblocks (used by scene images).
  [[nodiscard]] std::span<const CziSubblockInfo> SubblockSpan() const {
    return subblocks_;
  }

  /// @brief Per-channel metadata (name/color) parsed from the CZI XML.
  ///
  /// Indexed by channel (CZI "C") position. May be empty if the file carries
  /// no channel metadata; scene images then fall back to generated names.
  [[nodiscard]] const std::vector<ChannelMetadata>& MetadataChannels() const {
    return metadata_channels_;
  }

 private:
  friend class ReaderFactory<CziReader>;
  // Grants the Z/T plane-selection unit test access to build a reader with
  // synthetic subblocks (no file I/O) so scene-level plane planning can be
  // tested without a multi-Z/T sample file.
  friend struct CziReaderTestAccess;

  using Subblock = CziSubblockInfo;

  struct AttachmentInfo {
    std::string name;       ///< Native CZI attachment name (e.g. "Label").
    std::string file_type;  ///< e.g. "JPG" or "CZI".
    int64_t file_pos = 0;   ///< Segment start (file-relative).
  };

  explicit CziReader(std::string filename);

  static aifocore::Status ValidateInput(const fs::path& filename);
  static aifocore::Result<std::unique_ptr<CziReader>> CreateReaderImpl(
      const fs::path& filename);

  aifocore::Status Initialize();

  // Parsing helpers.
  aifocore::Status ParseFileHeader(FileReader& file);
  aifocore::Status ParseSubblockDirectory(FileReader& file);
  aifocore::Status ParseMetadataXml(FileReader& file);
  /// @brief Extract per-channel name/color from the parsed ImageDocument root.
  void ParseChannelMetadataXml(const pugi::xml_node& root);
  aifocore::Status ParseAttachmentDirectory(FileReader& file);
  void BuildSceneImages();

  [[nodiscard]] const CziSceneImage& Primary() const {
    return *images_[static_cast<size_t>(primary_index_)];
  }

  [[nodiscard]] const AttachmentInfo* FindAttachment(
      std::string_view name) const;

  // Header-derived offsets.
  int64_t subblk_dir_pos_ = 0;
  int64_t meta_pos_ = 0;
  int64_t att_dir_pos_ = 0;

  // Parsed data.
  std::string filename_;
  std::vector<Subblock> subblocks_;

  // Metadata fields (whole file).
  std::optional<ImageDimensions> metadata_size_l0_;
  std::optional<std::pair<double, double>> metadata_mpp_;
  std::optional<double> metadata_objective_magnification_;
  std::optional<double>
      metadata_z_spacing_um_;  ///< Focal-plane step (microns).
  std::optional<double> metadata_t_interval_s_;  ///< Time-point step (seconds).
  std::vector<ChannelMetadata>
      metadata_channels_;  ///< Per-channel name/color (CZI "C" order).
  std::string metadata_xml_;
  std::vector<AttachmentInfo> attachments_;

  // One image per scene; `primary_index_` is the lowest scene index.
  std::vector<std::unique_ptr<CziSceneImage>> images_;
  int primary_index_ = 0;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_H_
