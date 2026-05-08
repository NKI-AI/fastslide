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

/// @file dicom.cpp
/// @brief DICOM Whole-Slide Image reader implementation.
///
/// This implementation is based on the following DICOM standard sections:
///
///  - PS3.3 Section A.32.8 — VL Whole Slide Microscopy Image IOD
///  - PS3.3 Section C.7.6.1  — General Image Module (ImageType attribute)
///  - PS3.3 Section C.7.6.3  — Image Pixel Module (pixel format constraints)
///  - PS3.3 Section C.7.6.3.1.2 — PhotometricInterpretation / transfer syntax
///  - PS3.3 Section C.8.12.4 — Pixel Matrix (TotalPixelMatrix* attributes)
///  - PS3.3 Supplement 145   — Whole Slide Microscopy Image IOD & SOP Class
///
/// Frame indexing uses libdicom's position-based API
/// (dcm_filehandle_read_frame_position) to map (column, row) tile coordinates
/// to encoded pixel data frames.

#include "fastslide/readers/dicom/dicom.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

extern "C" {
#include <dicom/dicom.h>
}

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/metadata.h"
#include "fastslide/readers/dicom/dicom_decode.h"
#include "fastslide/readers/dicom/dicom_exec_context.h"
#include "fastslide/readers/dicom/dicom_magic.h"
#include "fastslide/readers/dicom/dicom_plan_builder.h"
#include "fastslide/readers/dicom/dicom_plan_context.h"
#include "fastslide/readers/dicom/dicom_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

namespace {

constexpr const char* kVLWholeSlideMicroscopyImageStorage =
    "1.2.840.10008.5.1.4.1.1.77.1.6";

// Resolve a DICOM keyword to its numeric tag.
uint32_t TagFromKeyword(const char* keyword) {
  return dcm_dict_tag_from_keyword(keyword);
}

// Get an integer value from a dataset by keyword.
std::optional<int64_t> GetTagInt(const DcmDataSet* dataset,
                                 const char* keyword) {
  uint32_t tag = TagFromKeyword(keyword);
  DcmElement* element = dcm_dataset_get(nullptr, dataset, tag);
  if (!element)
    return std::nullopt;
  int64_t result = 0;
  if (!dcm_element_get_value_integer(nullptr, element, 0, &result)) {
    return std::nullopt;
  }
  return result;
}

// Get a string value from a dataset by keyword.
std::optional<std::string_view> GetTagStr(const DcmDataSet* dataset,
                                          const char* keyword, int index = 0) {
  uint32_t tag = TagFromKeyword(keyword);
  DcmElement* element = dcm_dataset_get(nullptr, dataset, tag);
  if (!element)
    return std::nullopt;
  const char* value = nullptr;
  if (!dcm_element_get_value_string(nullptr, element, index, &value) ||
      !value) {
    return std::nullopt;
  }
  return std::string_view(value);
}

// Get a decimal string value as double.
std::optional<double> GetTagDecimalStr(const DcmDataSet* dataset,
                                       const char* keyword, int index = 0) {
  auto str = GetTagStr(dataset, keyword, index);
  if (!str)
    return std::nullopt;
  char* end = nullptr;
  double value = std::strtod(str->data(), &end);
  if (end == str->data() || std::isnan(value))
    return std::nullopt;
  return value;
}

// Read a tag whose value may be encoded numerically (UL/US/SL/SS/...) or
// as a DICOM Integer String (VR `IS`). libdicom's get_value_integer only
// accepts numeric VRs, so we transparently fall back to string parsing.
std::optional<int64_t> GetTagIntOrStr(const DcmDataSet* dataset,
                                      const char* keyword) {
  if (auto numeric = GetTagInt(dataset, keyword)) {
    return numeric;
  }
  auto str = GetTagStr(dataset, keyword);
  if (!str)
    return std::nullopt;
  char* end = nullptr;
  errno = 0;
  long long value = std::strtoll(str->data(), &end, 10);
  if (end == str->data() || errno != 0)
    return std::nullopt;
  return static_cast<int64_t>(value);
}

// Get a sequence from a dataset.
DcmSequence* GetTagSeq(const DcmDataSet* dataset, const char* keyword) {
  uint32_t tag = TagFromKeyword(keyword);
  DcmElement* element = dcm_dataset_get(nullptr, dataset, tag);
  if (!element)
    return nullptr;
  DcmSequence* seq = nullptr;
  if (!dcm_element_get_value_sequence(nullptr, element, &seq)) {
    return nullptr;
  }
  return seq;
}

// Get a sequence item dataset.
DcmDataSet* GetTagSeqItem(const DcmDataSet* dataset, const char* keyword,
                          uint32_t index) {
  DcmSequence* seq = GetTagSeq(dataset, keyword);
  if (!seq)
    return nullptr;
  return dcm_sequence_get(nullptr, seq, index);
}

// Verify an integer tag has the expected value.
aifocore::Status VerifyTagInt(const DcmDataSet* dataset,
                              const char* tag_description, int64_t expected,
                              bool required) {
  auto value = GetTagInt(dataset, tag_description);
  if (!value) {
    if (!required)
      return aifocore::Status::OkStatus();
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Missing tag: {}", tag_description));
  }
  if (*value != expected) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("{} = {} (expected {})", tag_description, *value,
                              expected));
  }
  return aifocore::Status::OkStatus();
}

/// @brief Purpose of a DICOM WSI file as determined from ImageType (PS3.3 C.7.6.1).
enum class DicomImagePurpose {
  kPyramidLevel,
  kLabel,
  kOverview,
  kThumbnail,
  kUnknown,
};

// Classify a DICOM file's purpose from its ImageType attribute.
// ImageType has value multiplicity 4 for WSI: derivation\primary\type\detail.
DicomImagePurpose ClassifyImageType(const DcmDataSet* metadata) {
  uint32_t tag = TagFromKeyword("ImageType");
  DcmElement* element = dcm_dataset_get(nullptr, metadata, tag);
  if (!element)
    return DicomImagePurpose::kUnknown;

  std::string components[4];
  for (int i = 0; i < 4; ++i) {
    const char* item = nullptr;
    if (!dcm_element_get_value_string(nullptr, element, i, &item) || !item) {
      return DicomImagePurpose::kUnknown;
    }
    components[i] = item;
  }

  // First two components must be (ORIGINAL|DERIVED) and PRIMARY.
  if ((components[0] != "ORIGINAL" && components[0] != "DERIVED") ||
      components[1] != "PRIMARY") {
    return DicomImagePurpose::kUnknown;
  }

  // Third component determines the image purpose per PS3.3 Table C.8.12.4-1.
  if (components[2] == "VOLUME") {
    if (components[3] == "NONE" || components[3] == "RESAMPLED") {
      return DicomImagePurpose::kPyramidLevel;
    }
  } else if (components[2] == "LABEL") {
    return DicomImagePurpose::kLabel;
  } else if (components[2] == "OVERVIEW") {
    return DicomImagePurpose::kOverview;
  } else if (components[2] == "THUMBNAIL") {
    return DicomImagePurpose::kThumbnail;
  }

  return DicomImagePurpose::kUnknown;
}

// Map a DicomImagePurpose to an associated image name, or nullopt for levels.
std::optional<std::string> AssociatedNameForPurpose(DicomImagePurpose purpose) {
  switch (purpose) {
    case DicomImagePurpose::kLabel:
      return "label";
    case DicomImagePurpose::kOverview:
      return "macro";
    case DicomImagePurpose::kThumbnail:
      return "thumbnail";
    default:
      return std::nullopt;
  }
}

// Convert DcmError to aifocore::Status and clear.
aifocore::Status DcmErrorToStatus(DcmError* dcm_error) {
  if (!dcm_error) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Unknown DICOM error");
  }
  std::string msg = dcm_error_get_summary(dcm_error);
  const char* detail = dcm_error_get_message(dcm_error);
  if (detail) {
    msg += ": ";
    msg += detail;
  }
  dcm_error_clear(&dcm_error);
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal, msg);
}

}  // namespace

DicomReader::DicomReader(std::string filename)
    : filename_(std::move(filename)) {}

aifocore::Status DicomReader::ValidateInput(const fs::path& filename) {
  std::error_code ec;
  auto status = fs::status(filename, ec);
  if (ec || !fs::exists(status)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Path not found: {}", filename.string()));
  }

  // Directories are accepted: Initialize() will look for a DICOM file inside.
  if (fs::is_directory(status)) {
    return aifocore::Status::OkStatus();
  }

  if (!fs::is_regular_file(status)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Not a regular file: {}", filename.string()));
  }

  // Accept either the conventional ".dcm" extension or any file that
  // carries the Part 10 DICOM magic ("DICM" at offset 128). The latter
  // covers extensionless DICOM exports such as those produced by
  // 3DHISTECH/MIRAX scanners.
  auto ext = filename.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  if (ext == ".dcm") {
    return aifocore::Status::OkStatus();
  }
  if (::fastslide::dicom::HasDicomMagic(filename)) {
    return aifocore::Status::OkStatus();
  }

  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kInvalidArgument,
      aifocore::fmt::format(
          "Not a DICOM file (no '.dcm' extension and missing DICM magic): {}",
          filename.string()));
}

DicomTransferSyntax DicomReader::ParseTransferSyntax(const char* uid) {
  if (!uid)
    return DicomTransferSyntax::kUnsupported;
  if (std::strcmp(uid, "1.2.840.10008.1.2.1") == 0) {
    return DicomTransferSyntax::kExplicitVRLittleEndian;
  }
  if (std::strcmp(uid, "1.2.840.10008.1.2.4.50") == 0) {
    return DicomTransferSyntax::kJpegBaseline;
  }
  if (std::strcmp(uid, "1.2.840.10008.1.2.4.90") == 0) {
    return DicomTransferSyntax::kJpeg2000Lossless;
  }
  if (std::strcmp(uid, "1.2.840.10008.1.2.4.91") == 0) {
    return DicomTransferSyntax::kJpeg2000;
  }
  return DicomTransferSyntax::kUnsupported;
}

aifocore::Result<std::shared_ptr<DicomFile>> DicomReader::OpenDicomFile(
    const std::string& filepath, bool load_metadata) {
  DcmError* dcm_error = nullptr;

  auto fh = UniqueDcmFilehandle(
      dcm_filehandle_create_from_file(&dcm_error, filepath.c_str()));
  if (!fh) {
    return DcmErrorToStatus(dcm_error);
  }

  // Read file meta.
  const DcmDataSet* file_meta =
      dcm_filehandle_get_file_meta(&dcm_error, fh.get());
  if (!file_meta) {
    return DcmErrorToStatus(dcm_error);
  }

  // Check MediaStorageSOPClassUID == VL Whole Slide Microscopy.
  auto sop_class = GetTagStr(file_meta, "MediaStorageSOPClassUID");
  if (!sop_class || *sop_class != kVLWholeSlideMicroscopyImageStorage) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Not a WSI DICOM: {}", filepath));
  }

  auto file = std::make_shared<DicomFile>();
  file->filepath = filepath;
  file->filehandle = std::move(fh);

  // Parse transfer syntax.
  const char* syntax_uid =
      dcm_filehandle_get_transfer_syntax_uid(file->filehandle.get());
  file->transfer_syntax = ParseTransferSyntax(syntax_uid);

  if (load_metadata) {
    const DcmDataSet* metadata =
        dcm_filehandle_get_metadata_subset(&dcm_error, file->filehandle.get());
    if (!metadata) {
      return DcmErrorToStatus(dcm_error);
    }

    // SeriesInstanceUID = slide identity.
    auto series_uid = GetTagStr(metadata, "SeriesInstanceUID");
    if (!series_uid) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Missing SeriesInstanceUID");
    }
    file->slide_id = std::string(*series_uid);

    // SOPInstanceUID for duplicate detection.
    if (auto sop_uid = GetTagStr(metadata, "SOPInstanceUID")) {
      file->sop_instance_uid = std::string(*sop_uid);
    }

    // Dimensions.
    if (auto total_cols = GetTagInt(metadata, "TotalPixelMatrixColumns")) {
      file->total_pixel_matrix_columns = static_cast<uint32_t>(*total_cols);
    }
    if (auto total_rows = GetTagInt(metadata, "TotalPixelMatrixRows")) {
      file->total_pixel_matrix_rows = static_cast<uint32_t>(*total_rows);
    }
    if (auto cols = GetTagInt(metadata, "Columns")) {
      file->columns = static_cast<uint32_t>(*cols);
    }
    if (auto rows = GetTagInt(metadata, "Rows")) {
      file->rows = static_cast<uint32_t>(*rows);
    }

    // Concatenation metadata (PS3.3 §C.7.6.16.2.2.4). All four tags are
    // optional; when ConcatenationUID is absent the file represents a
    // standalone, unsegmented level.
    if (auto concat_uid = GetTagStr(metadata, "ConcatenationUID")) {
      file->concatenation_uid = std::string(*concat_uid);
    }
    if (auto in_concat = GetTagInt(metadata, "InConcatenationNumber")) {
      file->in_concatenation_number = static_cast<uint32_t>(*in_concat);
    }
    if (auto frame_offset =
            GetTagInt(metadata, "ConcatenationFrameOffsetNumber")) {
      file->concatenation_frame_offset = static_cast<uint32_t>(*frame_offset);
    }
    // NumberOfFrames is VR `IS` (Integer String) per PS3.3 C.7.6.6, so it
    // requires the string-aware integer reader.
    if (auto num_frames = GetTagIntOrStr(metadata, "NumberOfFrames")) {
      file->number_of_frames = static_cast<uint32_t>(*num_frames);
    }
  }

  return file;
}

aifocore::Status DicomReader::AddFile(std::shared_ptr<DicomFile> file) {
  DcmError* dcm_error = nullptr;
  const DcmDataSet* metadata =
      dcm_filehandle_get_metadata_subset(&dcm_error, file->filehandle.get());
  if (!metadata) {
    return DcmErrorToStatus(dcm_error);
  }

  auto purpose = ClassifyImageType(metadata);

  if (purpose == DicomImagePurpose::kUnknown) {
    return aifocore::Status::OkStatus();
  }

  bool is_level = (purpose == DicomImagePurpose::kPyramidLevel);
  auto assoc_name = AssociatedNameForPurpose(purpose);

  if (!is_level && !assoc_name) {
    return aifocore::Status::OkStatus();
  }

  // Verify transfer syntax is supported.
  if (file->transfer_syntax == DicomTransferSyntax::kUnsupported) {
    const char* syntax_uid =
        dcm_filehandle_get_transfer_syntax_uid(file->filehandle.get());
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Unsupported transfer syntax: {}",
                              syntax_uid ? syntax_uid : "unknown"));
  }

  // Verify pixel format: 8-bit unsigned RGB, interleaved (PS3.3 C.7.6.3).
  // Sample description.
  AIFOCORE_RETURN_IF_ERROR(VerifyTagInt(metadata, "SamplesPerPixel", 3, true));
  AIFOCORE_RETURN_IF_ERROR(VerifyTagInt(metadata, "BitsAllocated", 8, true));
  AIFOCORE_RETURN_IF_ERROR(VerifyTagInt(metadata, "BitsStored", 8, true));
  AIFOCORE_RETURN_IF_ERROR(VerifyTagInt(metadata, "HighBit", 7, true));
  AIFOCORE_RETURN_IF_ERROR(
      VerifyTagInt(metadata, "PixelRepresentation", 0, true));
  // Memory layout.
  AIFOCORE_RETURN_IF_ERROR(
      VerifyTagInt(metadata, "PlanarConfiguration", 0, true));
  // Single focal plane only (optional tag).
  AIFOCORE_RETURN_IF_ERROR(
      VerifyTagInt(metadata, "TotalPixelMatrixFocalPlanes", 1, false));

  // Validate PhotometricInterpretation against the transfer syntax
  // (PS3.3 C.7.6.3.1.2). The allowed interpretations depend on the
  // compression scheme.
  {
    auto photometric = GetTagStr(metadata, "PhotometricInterpretation");
    if (!photometric) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Missing PhotometricInterpretation");
    }
    switch (file->transfer_syntax) {
      case DicomTransferSyntax::kExplicitVRLittleEndian:
        if (*photometric != "RGB") {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kInvalidArgument,
              aifocore::fmt::format("Uncompressed requires RGB, got {}",
                                    *photometric));
        }
        file->photometric = DicomPhotometric::kRgb;
        break;
      case DicomTransferSyntax::kJpegBaseline:
        if (*photometric == "YBR_FULL_422") {
          file->photometric = DicomPhotometric::kYbrFull422;
        } else if (*photometric == "RGB") {
          file->photometric = DicomPhotometric::kRgb;
        } else {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kInvalidArgument,
              aifocore::fmt::format("JPEG requires YBR_FULL_422 or RGB, got {}",
                                    *photometric));
        }
        break;
      case DicomTransferSyntax::kJpeg2000:
      case DicomTransferSyntax::kJpeg2000Lossless:
        if (*photometric == "YBR_ICT") {
          file->photometric = DicomPhotometric::kYbrIct;
        } else if (*photometric == "RGB") {
          file->photometric = DicomPhotometric::kRgb;
        } else {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kInvalidArgument,
              aifocore::fmt::format("JPEG 2000 requires YBR_ICT or RGB, got {}",
                                    *photometric));
        }
        break;
      default:
        break;
    }
  }

  if (is_level) {
    const uint32_t level_width = file->total_pixel_matrix_columns;
    const uint32_t level_height = file->total_pixel_matrix_rows;
    const uint32_t level_tile_w = file->columns;
    const uint32_t level_tile_h = file->rows;

    // Find an existing level with matching dimensions. Two files at the
    // same pyramid resolution must either:
    //   1. be the same SOP Instance (benign on-disk duplicate),
    //   2. be sibling parts of the same DICOM concatenation, or
    //   3. genuinely conflict — which we surface as an error.
    for (auto& existing : levels_) {
      if (existing.width != level_width || existing.height != level_height) {
        continue;
      }

      const auto& existing_primary = existing.PrimaryFile();
      const bool same_concatenation =
          !file->concatenation_uid.empty() &&
          file->concatenation_uid == existing_primary.concatenation_uid;
      const bool different_concatenation_status =
          file->concatenation_uid.empty() !=
          existing_primary.concatenation_uid.empty();

      if (different_concatenation_status ||
          (!file->concatenation_uid.empty() && !same_concatenation)) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kFailedPrecondition,
            aifocore::fmt::format(
                "Slide contains conflicting pyramid level data at {}x{}: "
                "'{}' (Concatenation UID '{}') does not match the existing "
                "'{}' (Concatenation UID '{}')",
                level_width, level_height, file->filepath,
                file->concatenation_uid, existing_primary.filepath,
                existing_primary.concatenation_uid));
      }

      if (same_concatenation) {
        // Sibling concatenation part. Reject if we already have a part at
        // the same In-Concatenation slot with a different SOP Instance UID.
        for (const auto& part : existing.parts) {
          if (part.file->in_concatenation_number ==
                  file->in_concatenation_number &&
              part.file->sop_instance_uid != file->sop_instance_uid) {
            return AIFOCORE_MAKE_STATUS(
                aifocore::StatusCode::kFailedPrecondition,
                aifocore::fmt::format(
                    "Conflicting DICOM concatenation parts at slot {}: "
                    "'{}' (SOP Instance UID {}) does not match '{}' "
                    "(SOP Instance UID {})",
                    file->in_concatenation_number, file->filepath,
                    file->sop_instance_uid, part.file->filepath,
                    part.file->sop_instance_uid));
          }
          if (part.file->sop_instance_uid == file->sop_instance_uid) {
            // Benign duplicate copy of a part we already have.
            return aifocore::Status::OkStatus();
          }
        }

        DicomLevelPart new_part;
        new_part.file = file;
        new_part.frame_offset = file->concatenation_frame_offset;
        new_part.frame_count = file->number_of_frames;
        const auto insert_at = std::lower_bound(
            existing.parts.begin(), existing.parts.end(), new_part,
            [](const DicomLevelPart& a, const DicomLevelPart& b) {
              return a.frame_offset < b.frame_offset;
            });
        existing.parts.insert(insert_at, std::move(new_part));
        return aifocore::Status::OkStatus();
      }

      // No concatenation on either side: classic SOP-instance duplicate
      // check (cf. OpenSlide's ensure_sop_instance_uids_equal).
      if (!file->sop_instance_uid.empty() &&
          !existing_primary.sop_instance_uid.empty() &&
          file->sop_instance_uid != existing_primary.sop_instance_uid) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kFailedPrecondition,
            aifocore::fmt::format(
                "Slide contains conflicting pyramid level data at {}x{}: "
                "'{}' (SOP Instance UID {}) does not match the existing "
                "'{}' (SOP Instance UID {})",
                level_width, level_height, file->filepath,
                file->sop_instance_uid, existing_primary.filepath,
                existing_primary.sop_instance_uid));
      }
      return aifocore::Status::OkStatus();
    }

    DicomLevel level;
    level.width = level_width;
    level.height = level_height;
    level.tile_w = level_tile_w;
    level.tile_h = level_tile_h;
    DicomLevelPart part;
    part.file = file;
    part.frame_offset = file->concatenation_frame_offset;
    part.frame_count = file->number_of_frames;
    level.parts.push_back(std::move(part));

    ExtractPixelSpacing(metadata, level);
    ExtractObjectiveLensPower(metadata, level);

    levels_.push_back(std::move(level));
  } else {
    DicomAssociated assoc;
    assoc.file = file;
    assoc.name = *assoc_name;
    assoc.width = file->total_pixel_matrix_columns;
    assoc.height = file->total_pixel_matrix_rows;

    for (const auto& existing : associated_) {
      if (existing.name == assoc.name) {
        if (!file->sop_instance_uid.empty() &&
            !existing.file->sop_instance_uid.empty() &&
            file->sop_instance_uid != existing.file->sop_instance_uid) {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kFailedPrecondition,
              aifocore::fmt::format(
                  "Slide contains conflicting '{}' associated images: '{}' "
                  "(SOP Instance UID {}) does not match the existing '{}' "
                  "(SOP Instance UID {})",
                  assoc.name, file->filepath, file->sop_instance_uid,
                  existing.file->filepath, existing.file->sop_instance_uid));
        }
        return aifocore::Status::OkStatus();
      }
    }

    associated_.push_back(std::move(assoc));
  }

  return aifocore::Status::OkStatus();
}

void DicomReader::ExtractPixelSpacing(const DcmDataSet* metadata,
                                      DicomLevel& level) {
  DcmDataSet* shared_fg =
      GetTagSeqItem(metadata, "SharedFunctionalGroupsSequence", 0);
  if (!shared_fg)
    return;
  DcmDataSet* pixel_measures =
      GetTagSeqItem(shared_fg, "PixelMeasuresSequence", 0);
  if (!pixel_measures)
    return;

  if (auto spacing_x = GetTagDecimalStr(pixel_measures, "PixelSpacing", 0)) {
    level.pixel_spacing_x = *spacing_x;
  }
  if (auto spacing_y = GetTagDecimalStr(pixel_measures, "PixelSpacing", 1)) {
    level.pixel_spacing_y = *spacing_y;
  }
}

void DicomReader::ExtractObjectiveLensPower(const DcmDataSet* metadata,
                                            DicomLevel& level) {
  DcmDataSet* optical_path = GetTagSeqItem(metadata, "OpticalPathSequence", 0);
  if (!optical_path)
    return;
  if (auto power = GetTagDecimalStr(optical_path, "ObjectiveLensPower")) {
    level.objective_lens_power = *power;
  }
}

aifocore::Status DicomReader::Initialize() {
  // If the user pointed us at a directory, find the first DICOM file
  // inside and use that as the starting point. The sibling scan below will
  // then pick up the rest of the series.
  std::error_code ec;
  if (fs::is_directory(filename_, ec)) {
    fs::path chosen;
    for (const auto& entry : fs::directory_iterator(filename_, ec)) {
      if (ec) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            aifocore::fmt::format("Failed to scan directory '{}': {}",
                                  filename_, ec.message()));
      }
      if (!entry.is_regular_file(ec) || ec) {
        continue;
      }
      // Prefer a `.dcm` file if present, otherwise any file with the magic.
      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".dcm" || ::fastslide::dicom::HasDicomMagic(entry.path())) {
        chosen = entry.path();
        if (ext == ".dcm") {
          break;  // Strong signal — stop searching.
        }
      }
    }
    if (chosen.empty()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kNotFound,
          aifocore::fmt::format("No DICOM files found in directory: {}",
                                filename_));
    }
    filename_ = chosen.string();
  }

  AIFOCORE_ASSIGN_OR_RETURN(auto start_file, OpenDicomFile(filename_, true));
  std::string slide_id = start_file->slide_id;

  AIFOCORE_RETURN_IF_ERROR(AddFile(start_file));

  // Scan directory for sibling DICOM files. We accept both the canonical
  // ".dcm" extension and extensionless files that carry the Part 10 DICOM
  // magic — vendors like 3DHISTECH ship slides as numbered, extensionless
  // files in a directory.
  fs::path parent = fs::path(filename_).parent_path();
  std::string start_basename = fs::path(filename_).filename().string();

  if (fs::is_directory(parent)) {
    for (const auto& entry : fs::directory_iterator(parent)) {
      if (!entry.is_regular_file())
        continue;
      std::string name = entry.path().filename().string();
      if (name == start_basename)
        continue;

      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      const bool has_dcm_ext = (ext == ".dcm");
      if (!has_dcm_ext && !::fastslide::dicom::HasDicomMagic(entry.path())) {
        continue;
      }

      auto sibling_or = OpenDicomFile(entry.path().string(), true);
      if (!sibling_or.ok())
        continue;

      auto sibling = std::move(*sibling_or);
      if (sibling->slide_id != slide_id)
        continue;

      // Past this point the sibling claims to belong to the same series.
      // A conflict (kFailedPrecondition) means the slide on disk is
      // inconsistent — we mirror OpenSlide and propagate the error so the
      // user knows exactly which files disagree, instead of quietly
      // dropping data. Other errors (unsupported transfer syntax, missing
      // fields, ...) only mean we cannot use this particular sibling, so
      // we keep going.
      auto add_st = AddFile(sibling);
      if (!add_st.ok()) {
        if (add_st.code() == aifocore::StatusCode::kFailedPrecondition) {
          return add_st;
        }
        continue;
      }
    }
  }

  if (levels_.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "No pyramid levels found");
  }

  // Sort levels by width descending (level 0 = largest).
  std::sort(levels_.begin(), levels_.end(),
            [](const DicomLevel& a, const DicomLevel& b) {
              return a.width > b.width;
            });

  // Compute downsamples relative to level 0.
  const double l0_w = static_cast<double>(levels_[0].width);
  const double l0_h = static_cast<double>(levels_[0].height);
  levels_[0].downsample = 1.0;
  for (size_t i = 1; i < levels_.size(); ++i) {
    double ds_w = l0_w / static_cast<double>(levels_[i].width);
    double ds_h = l0_h / static_cast<double>(levels_[i].height);
    levels_[i].downsample = (ds_w + ds_h) / 2.0;
  }

  // Populate properties from level 0.
  const auto& l0 = levels_[0];
  if (l0.pixel_spacing_x) {
    properties_.mpp[0] = *l0.pixel_spacing_x * 1000.0;
  }
  if (l0.pixel_spacing_y) {
    properties_.mpp[1] = *l0.pixel_spacing_y * 1000.0;
  }
  if (l0.objective_lens_power) {
    properties_.objective_magnification = *l0.objective_lens_power;
  }

  return aifocore::Status::OkStatus();
}

aifocore::Result<std::unique_ptr<DicomReader>> DicomReader::CreateReaderImpl(
    const fs::path& filename) {
  auto reader =
      std::unique_ptr<DicomReader>(new DicomReader(filename.string()));
  AIFOCORE_RETURN_IF_ERROR(reader->Initialize());
  return reader;
}

int DicomReader::GetLevelCount() const {
  return static_cast<int>(levels_.size());
}

aifocore::Result<LevelInfo> DicomReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= static_cast<int>(levels_.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }
  const auto& l = levels_[level];
  return LevelInfo({l.width, l.height}, l.downsample);
}

const SlideProperties& DicomReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> DicomReader::GetChannelMetadata() const {
  return {
      ChannelMetadata("Red", "", ColorRGB{255, 0, 0}),
      ChannelMetadata("Green", "", ColorRGB{0, 255, 0}),
      ChannelMetadata("Blue", "", ColorRGB{0, 0, 255}),
  };
}

std::vector<std::string> DicomReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_.size());
  for (const auto& a : associated_) {
    names.push_back(a.name);
  }
  return names;
}

aifocore::Result<ImageDimensions> DicomReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  for (const auto& a : associated_) {
    if (a.name == name) {
      return ImageDimensions{a.width, a.height};
    }
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image not found: {}", name));
}

aifocore::Result<Image> DicomReader::ReadAssociatedImage(
    std::string_view name) const {
  const DicomAssociated* target = nullptr;
  for (const auto& a : associated_) {
    if (a.name == name) {
      target = &a;
      break;
    }
  }
  if (!target) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image not found: {}", name));
  }

  // Read frame (0, 0) from the associated file.
  auto& file = target->file;
  std::lock_guard<std::mutex> lock(file->mutex);

  DcmError* dcm_error = nullptr;
  UniqueDcmFrame frame(dcm_filehandle_read_frame_position(
      &dcm_error, file->filehandle.get(), 0, 0));
  if (!frame) {
    return DcmErrorToStatus(dcm_error);
  }

  const auto* raw =
      reinterpret_cast<const uint8_t*>(dcm_frame_get_value(frame.get()));
  const uint32_t length = dcm_frame_get_length(frame.get());

  const uint32_t w = target->width;
  const uint32_t h = target->height;

  AIFOCORE_ASSIGN_OR_RETURN(
      auto rgb, dicom::internal::DecodeDicomFrameBytes(
                    std::span<const uint8_t>(raw, length),
                    file->transfer_syntax, file->photometric, w, h));

  Image image({w, h}, ImageFormat::kRGB, DataType::kUInt8);
  if (rgb.size() != image.SizeBytes()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format(
            "Decoded DICOM associated image size mismatch: got {} bytes, "
            "expected {}",
            rgb.size(), image.SizeBytes()));
  }
  std::memcpy(image.GetData(), rgb.data(), rgb.size());
  return image;
}

Metadata DicomReader::GetMetadata() const {
  Metadata meta;
  meta["dicom.format"] = "DICOM";
  if (!levels_.empty()) {
    const auto& l0 = levels_[0];
    meta["dicom.level_count"] = std::to_string(levels_.size());
    if (l0.pixel_spacing_x) {
      meta["dicom.pixel_spacing_x"] = std::to_string(*l0.pixel_spacing_x);
    }
    if (l0.pixel_spacing_y) {
      meta["dicom.pixel_spacing_y"] = std::to_string(*l0.pixel_spacing_y);
    }
    if (l0.objective_lens_power) {
      meta["dicom.objective_lens_power"] =
          std::to_string(*l0.objective_lens_power);
    }
    if (!l0.parts.empty()) {
      meta["dicom.series_instance_uid"] = l0.PrimaryFile().slide_id;
    }
  }
  return meta;
}

ImageDimensions DicomReader::GetTileSize() const {
  if (levels_.empty())
    return {0, 0};
  return {levels_[0].tile_w, levels_[0].tile_h};
}

aifocore::Result<core::TilePlan> DicomReader::PrepareRequest(
    const core::TileRequest& request) const {
  AIFOCORE_ASSIGN_OR_RETURN(const auto level_info, GetLevelInfo(request.level));
  const DicomPlanContext context{
      .levels = levels_,
      .level_info = level_info,
  };
  return DicomPlanBuilder::BuildPlan(request, context);
}

aifocore::Status DicomReader::ExecutePlan(const core::TilePlan& plan,
                                          runtime::Canvas& writer) const {
  const DicomExecContext context(filename_, levels_, GetCache());
  return DicomTileExecutor::ExecutePlan(plan, context, writer);
}

}  // namespace fastslide
