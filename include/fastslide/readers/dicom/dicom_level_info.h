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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_LEVEL_INFO_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <dicom/dicom.h>
}

namespace fastslide {

/// @brief Supported DICOM transfer syntaxes for pixel data decoding.
enum class DicomTransferSyntax {
  kUnsupported,
  kExplicitVRLittleEndian,
  kJpegBaseline,
  kJpeg2000Lossless,
  kJpeg2000,
};

/// @brief RAII wrapper for DcmFilehandle.
struct DcmFilehandleDeleter {
  void operator()(DcmFilehandle* fh) const {
    if (fh != nullptr) {
      dcm_filehandle_destroy(fh);
    }
  }
};

using UniqueDcmFilehandle =
    std::unique_ptr<DcmFilehandle, DcmFilehandleDeleter>;

/// @brief RAII wrapper for DcmFrame.
struct DcmFrameDeleter {
  void operator()(DcmFrame* frame) const {
    if (frame != nullptr) {
      dcm_frame_destroy(frame);
    }
  }
};

using UniqueDcmFrame = std::unique_ptr<DcmFrame, DcmFrameDeleter>;

/// @brief Validated photometric interpretation for this file.
enum class DicomPhotometric {
  kRgb,
  kYbrFull422,
  kYbrIct,
};

/// @brief One parsed DICOM file (one SOP instance).
struct DicomFile {
  UniqueDcmFilehandle filehandle;
  DicomTransferSyntax transfer_syntax = DicomTransferSyntax::kUnsupported;
  DicomPhotometric photometric = DicomPhotometric::kRgb;
  std::string slide_id;
  std::string sop_instance_uid;
  std::string filepath;

  uint32_t total_pixel_matrix_columns = 0;
  uint32_t total_pixel_matrix_rows = 0;
  uint32_t columns = 0;
  uint32_t rows = 0;

  std::string concatenation_uid;
  uint32_t in_concatenation_number = 0;
  uint32_t concatenation_frame_offset = 0;
  uint32_t number_of_frames = 0;

  mutable std::mutex mutex;
};

/// @brief A single concatenation part contributing to a pyramid level.
struct DicomLevelPart {
  std::shared_ptr<DicomFile> file;
  uint32_t frame_offset = 0;
  uint32_t frame_count = 0;
};

/// @brief One pyramid level, possibly split across several DICOM files.
struct DicomLevel {
  std::vector<DicomLevelPart> parts;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t tile_w = 0;
  uint32_t tile_h = 0;
  double downsample = 1.0;
  std::optional<double> pixel_spacing_x;
  std::optional<double> pixel_spacing_y;
  std::optional<double> objective_lens_power;

  [[nodiscard]] const DicomFile& PrimaryFile() const {
    return *parts.front().file;
  }

  [[nodiscard]] const DicomLevelPart* FindPartForFrame(
      uint32_t frame_index) const {
    for (const auto& part : parts) {
      if (frame_index >= part.frame_offset &&
          frame_index < part.frame_offset + part.frame_count) {
        return &part;
      }
    }
    return nullptr;
  }
};

/// @brief One associated image (label, overview, thumbnail).
struct DicomAssociated {
  std::shared_ptr<DicomFile> file;
  std::string name;
  uint32_t width = 0;
  uint32_t height = 0;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_LEVEL_INFO_H_
