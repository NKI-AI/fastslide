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

extern "C" {
#include <dicom/dicom.h>
}

#include "aifocore/status/result.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/readers/reader_factory.h"
#include "fastslide/slide_reader.h"

namespace fastslide {

namespace fs = std::filesystem;

/// @brief Supported DICOM transfer syntaxes for pixel data decoding.
enum class DicomTransferSyntax {
  kUnsupported,
  kExplicitVRLittleEndian,  // 1.2.840.10008.1.2.1 (uncompressed)
  kJpegBaseline,            // 1.2.840.10008.1.2.4.50
  kJpeg2000Lossless,        // 1.2.840.10008.1.2.4.90
  kJpeg2000,                // 1.2.840.10008.1.2.4.91
};

/// @brief RAII wrapper for DcmFilehandle.
struct DcmFilehandleDeleter {
  void operator()(DcmFilehandle* fh) const {
    if (fh)
      dcm_filehandle_destroy(fh);
  }
};

using UniqueDcmFilehandle =
    std::unique_ptr<DcmFilehandle, DcmFilehandleDeleter>;

/// @brief RAII wrapper for DcmFrame.
struct DcmFrameDeleter {
  void operator()(DcmFrame* frame) const {
    if (frame)
      dcm_frame_destroy(frame);
  }
};

using UniqueDcmFrame = std::unique_ptr<DcmFrame, DcmFrameDeleter>;

/// @brief Validated photometric interpretation for this file.
enum class DicomPhotometric {
  kRgb,         // RGB color model
  kYbrFull422,  // YCbCr 4:2:2 (JPEG baseline)
  kYbrIct,      // Irreversible Color Transform (JPEG 2000 lossy)
};

/// @brief One parsed DICOM file (one SOP instance).
///
/// Wraps a libdicom filehandle plus cached metadata needed for reading.
struct DicomFile {
  UniqueDcmFilehandle filehandle;
  DicomTransferSyntax transfer_syntax = DicomTransferSyntax::kUnsupported;
  DicomPhotometric photometric = DicomPhotometric::kRgb;
  std::string slide_id;  // SeriesInstanceUID
  std::string sop_instance_uid;
  std::string filepath;

  uint32_t total_pixel_matrix_columns = 0;
  uint32_t total_pixel_matrix_rows = 0;
  uint32_t columns = 0;  // Per-frame (tile) width.
  uint32_t rows = 0;     // Per-frame (tile) height.

  mutable std::mutex mutex;
};

/// @brief One pyramid level backed by a DicomFile.
struct DicomLevel {
  std::shared_ptr<DicomFile> file;
  uint32_t width = 0;   // TotalPixelMatrixColumns
  uint32_t height = 0;  // TotalPixelMatrixRows
  uint32_t tile_w = 0;  // Columns (per frame)
  uint32_t tile_h = 0;  // Rows (per frame)
  double downsample = 1.0;
  std::optional<double> pixel_spacing_x;
  std::optional<double> pixel_spacing_y;
  std::optional<double> objective_lens_power;
};

/// @brief One associated image (label, overview, thumbnail).
struct DicomAssociated {
  std::shared_ptr<DicomFile> file;
  std::string name;
  uint32_t width = 0;
  uint32_t height = 0;
};

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

  /// @brief Parse the DICOM transfer syntax UID string.
  static DicomTransferSyntax ParseTransferSyntax(const char* uid);

  std::string filename_;
  std::vector<DicomLevel> levels_;
  std::vector<DicomAssociated> associated_;
  SlideProperties properties_{};
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_H_
