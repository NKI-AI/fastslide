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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/dicom/dicom_level_info.h"
#include "fastslide/readers/reader_factory.h"
#include "fastslide/slide_reader.h"

namespace fastslide {

namespace fs = std::filesystem;

/// @brief DICOM Whole-Slide Image reader.
///
/// Implements reading of DICOM WSI files per the VL Whole Slide Microscopy
/// Image Storage SOP class (1.2.840.10008.5.1.4.1.1.77.1.6), as defined
/// in DICOM PS3.3 Section A.32.8 and Supplement 145.
///
/// Structure:
/// - One .dcm file per pyramid level or associated image
/// - ImageType (PS3.3 C.7.6.1) classifies files: VOLUME values indicate
///   pyramid levels; LABEL/OVERVIEW/THUMBNAIL indicate associated images
/// - Frames map to tiles via libdicom's dcm_filehandle_read_frame_position()
/// - Files sharing a SeriesInstanceUID are grouped into one slide
class DicomReader : public SlideReader, public ReaderFactory<DicomReader> {
 public:
  static aifocore::Result<std::unique_ptr<DicomReader>> Create(
      std::string_view filename) {
    return CreateImpl(fs::path(filename));
  }

  ~DicomReader() override = default;

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

  /// @brief Embedded ICC profile from the Optical Path Sequence (0048,0105 →
  /// 0028,2000), if present.
  /// @return ICC profile bytes, or `kNotFound` when the slide carries none.
  [[nodiscard]] aifocore::Result<std::vector<uint8_t>> GetIccProfile()
      const override;

  std::string GetFormatName() const override { return "DICOM"; }

  ImageFormat GetImageFormat() const override { return ImageFormat::kRGB; }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  ImageDimensions GetTileSize() const override;

  aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  aifocore::Status ExecutePlan(const core::TilePlan& plan,
                               runtime::Canvas& writer) const override;

  /// @brief Get the filepath of the starting file (for cache keys).
  [[nodiscard]] const std::string& GetFilename() const { return filename_; }

  /// @brief Get level metadata for the tile executor.
  [[nodiscard]] const DicomLevel& GetLevel(int level) const {
    return levels_[level];
  }

 private:
  friend class ReaderFactory<DicomReader>;

  explicit DicomReader(std::string filename);

  static aifocore::Status ValidateInput(const fs::path& filename);
  static aifocore::Result<std::unique_ptr<DicomReader>> CreateReaderImpl(
      const fs::path& filename);

  aifocore::Status Initialize();

  /// @brief Open a single DICOM file and extract required metadata.
  static aifocore::Result<std::shared_ptr<DicomFile>> OpenDicomFile(
      const std::string& filepath, bool load_metadata);

  /// @brief Classify a file as level or associated image and add it.
  aifocore::Status AddFile(std::shared_ptr<DicomFile> file);

  /// @brief Extract pixel spacing from SharedFunctionalGroupsSequence.
  static void ExtractPixelSpacing(const DcmDataSet* metadata,
                                  DicomLevel& level);

  /// @brief Extract objective lens power from OpticalPathSequence.
  static void ExtractObjectiveLensPower(const DcmDataSet* metadata,
                                        DicomLevel& level);

  /// @brief Extract the embedded ICC profile from the OpticalPathSequence.
  /// @return ICC profile bytes, or `kNotFound` when absent/empty.
  static aifocore::Result<std::vector<uint8_t>> ExtractIccProfile(
      const DcmDataSet* metadata);

  /// @brief Parse the DICOM transfer syntax UID string.
  static DicomTransferSyntax ParseTransferSyntax(const char* uid);

  std::string filename_;
  std::vector<DicomLevel> levels_;
  std::vector<DicomAssociated> associated_;
  SlideProperties properties_{};
  std::vector<uint8_t> icc_profile_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_H_
