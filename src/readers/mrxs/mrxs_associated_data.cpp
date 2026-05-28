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

/// @file mrxs_associated_data.cpp
/// @brief Implementation of MRXS associated-data helpers.

#include "fastslide/readers/mrxs/mrxs_associated_data.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/mrxs/mrxs_data_reader.h"
#include "fastslide/readers/mrxs/mrxs_decoder.h"
#include "fastslide/readers/mrxs/mrxs_index_reader.h"
#include "fastslide/runtime/io/binary_utils.h"

namespace fastslide {
namespace mrxs {
namespace {

/// @brief Compute the public-facing record name for a non-hierarchical entry.
std::string MakeRecordName(const NonHierarchicalLayer& layer,
                           const NonHierarchicalRecord& record) {
  if (record.value_name.empty()) {
    return aifocore::fmt::format("{}_{}", layer.name, record.layer_index);
  }
  return record.value_name;
}

/// @brief Locate a record by its public name.
///
/// Returns nullptr if no record matches; otherwise returns a pointer that is
/// valid for the lifetime of @p slide_info. The owning layer is written to
/// `*out_layer` so callers can access `layer.name` for diagnostics.
const NonHierarchicalRecord* FindRecord(
    const SlideDataInfo& slide_info, std::string_view name,
    const NonHierarchicalLayer** out_layer) {
  for (const auto& layer : slide_info.nonhier_layers) {
    for (const auto& record : layer.records) {
      if (MakeRecordName(layer, record) == name) {
        if (out_layer != nullptr) {
          *out_layer = &layer;
        }
        return &record;
      }
    }
  }
  if (out_layer != nullptr) {
    *out_layer = nullptr;
  }
  return nullptr;
}

/// @brief Resolve a non-hierarchical record's data location via Index.dat.
struct ResolvedRecord {
  fs::path datafile_path;
  int64_t offset;
  int64_t size;
};

aifocore::Result<ResolvedRecord> ResolveRecord(const fs::path& dirname,
                                               const SlideDataInfo& slide_info,
                                               int record_index) {
  fs::path index_path = dirname / slide_info.index_filename;
  MrxsIndexReader index_reader;
  AIFOCORE_ASSIGN_OR_RETURN(index_reader,
                            MrxsIndexReader::Open(index_path, slide_info));

  NonHierRecordData record_data;
  AIFOCORE_ASSIGN_OR_RETURN(record_data,
                            index_reader.ReadNonHierRecord(record_index));

  return ResolvedRecord{
      .datafile_path = dirname / record_data.datafile_path,
      .offset = record_data.offset,
      .size = record_data.size,
  };
}

/// @brief Detect the wire image format from a decompressed payload.
MrxsImageFormat DetectImageFormat(const std::vector<uint8_t>& data) {
  if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
    return MrxsImageFormat::kJpeg;
  }
  if (data.size() >= 4 && data[0] == 0x89 && data[1] == 0x50) {
    return MrxsImageFormat::kPng;
  }
  if (data.size() >= 2 && data[0] == 0x42 && data[1] == 0x4D) {
    return MrxsImageFormat::kBmp;
  }
  if (data.size() >= 4 && data[0] == 0x49 && data[1] == 0x49 &&
      data[2] == 0xBC) {
    // JPEG-XR/HD Photo magic: "II" + 0xBC followed by codec stream marker.
    return MrxsImageFormat::kJpegXr;
  }
  return MrxsImageFormat::kUnknown;
}

/// @brief Inflate a zlib-wrapped payload using a generous size budget.
///
/// `raw_data` is consumed (moved-from) on success only. On failure the
/// caller's bytes remain valid and a warning is logged to stderr.
struct DecompressResult {
  std::vector<uint8_t> data;
  bool was_compressed = false;
};

DecompressResult MaybeInflateAssociatedPayload(
    std::vector<uint8_t> raw_data, std::string_view diagnostic_name) {
  const bool has_zlib_prefix =
      raw_data.size() >= 3 && raw_data[0] == 0x78 && raw_data[1] == 0x9C;
  if (!has_zlib_prefix) {
    return {std::move(raw_data), /*was_compressed=*/false};
  }

  // Expected size is unknown ahead of time; allow up to 100x expansion.
  const size_t expected_size = raw_data.size() * 100;
  auto decompressed_or =
      DecompressZlib(raw_data.data(), raw_data.size(), expected_size);
  if (decompressed_or.ok()) {
    return {std::move(*decompressed_or), /*was_compressed=*/true};
  }
  std::cerr << "Failed to decompress data for " << diagnostic_name << ": "
            << decompressed_or.status().ToString();
  return {std::move(raw_data), /*was_compressed=*/true};
}

/// @brief Convert a decoded payload into the appropriate `AssociatedData::data`
///        variant, based on @p type. Decoding failures bubble up.
aifocore::Status PopulateAssociatedDataPayload(AssociatedDataType type,
                                               std::vector<uint8_t> payload,
                                               AssociatedData& out) {
  switch (type) {
    case AssociatedDataType::kImage: {
      const MrxsImageFormat img_format = DetectImageFormat(payload);
      RGBImage decoded_image;
      AIFOCORE_ASSIGN_OR_RETURN(decoded_image,
                                internal::DecodeImage(payload, img_format));
      out.data = std::move(decoded_image);
      break;
    }
    case AssociatedDataType::kXml:
      out.data = std::string(payload.begin(), payload.end());
      break;
    default:
      out.data = std::move(payload);
      break;
  }
  return aifocore::Status::OkStatus();
}

}  // namespace

std::vector<std::string> MrxsAssociatedData::GetNames(
    const SlideDataInfo& slide_info) {
  std::vector<std::string> names;
  for (const auto& layer : slide_info.nonhier_layers) {
    for (const auto& record : layer.records) {
      names.push_back(MakeRecordName(layer, record));
    }
  }
  return names;
}

std::vector<std::string> MrxsAssociatedData::GetImageNames(
    const SlideDataInfo& slide_info) {
  std::vector<std::string> names;
  for (auto& full_name : GetNames(slide_info)) {
    if (full_name.find(internal::kAssociatedImagePrefix) == 0) {
      names.emplace_back(
          full_name.substr(internal::kAssociatedImagePrefix.size()));
    }
  }
  return names;
}

std::vector<std::string> MrxsAssociatedData::GetNonImageNames(
    const SlideDataInfo& slide_info) {
  std::vector<std::string> names;
  for (auto& name : GetNames(slide_info)) {
    if (name.find(internal::kAssociatedImagePrefix) != 0) {
      names.push_back(std::move(name));
    }
  }
  return names;
}

aifocore::Result<AssociatedDataInfo> MrxsAssociatedData::GetInfo(
    const SlideDataInfo& slide_info, std::string_view name) {
  const NonHierarchicalLayer* layer = nullptr;
  const NonHierarchicalRecord* record = FindRecord(slide_info, name, &layer);
  if (record == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated data not found: {}", name));
  }

  AssociatedDataInfo info;
  info.name = MakeRecordName(*layer, *record);
  info.description = aifocore::fmt::format("Layer: {}", layer->name);
  info.size_bytes = 0;
  info.is_compressed = false;
  info.compression_type = "unknown";
  info.type = AssociatedDataType::kUnknown;
  return info;
}

aifocore::Result<AssociatedData> MrxsAssociatedData::Load(
    const fs::path& dirname, const SlideDataInfo& slide_info,
    std::string_view name) {
  const NonHierarchicalLayer* layer = nullptr;
  const NonHierarchicalRecord* target_record =
      FindRecord(slide_info, name, &layer);
  if (target_record == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated data not found: {}", name));
  }

  ResolvedRecord resolved;
  AIFOCORE_ASSIGN_OR_RETURN(
      resolved,
      ResolveRecord(dirname, slide_info, target_record->record_index));

  std::vector<uint8_t> raw_data;
  AIFOCORE_ASSIGN_OR_RETURN(
      raw_data, MrxsDataReader::ReadData(resolved.datafile_path,
                                         resolved.offset, resolved.size));

  AssociatedDataType type = DetectDataType(raw_data);

  DecompressResult decompressed =
      MaybeInflateAssociatedPayload(std::move(raw_data), name);
  if (decompressed.was_compressed) {
    // Re-detect type now that the payload is in its final form.
    type = DetectDataType(decompressed.data);
  }

  AssociatedData result;
  result.info.name = std::string(name);
  result.info.description = aifocore::fmt::format("Layer: {}", layer->name);
  result.info.size_bytes = decompressed.data.size();
  result.info.is_compressed = decompressed.was_compressed;
  result.info.compression_type = decompressed.was_compressed ? "zlib" : "none";
  result.info.type = type;

  AIFOCORE_RETURN_IF_ERROR(PopulateAssociatedDataPayload(
      type, std::move(decompressed.data), result));
  return result;
}

aifocore::Result<RGBImage> MrxsAssociatedData::ReadImage(
    const fs::path& dirname, const SlideDataInfo& slide_info,
    std::string_view name) {
  std::string full_name =
      std::string(internal::kAssociatedImagePrefix) + std::string(name);
  AssociatedData data;
  AIFOCORE_ASSIGN_OR_RETURN(data, Load(dirname, slide_info, full_name));
  if (!data.IsImage()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image not found: {}", name));
  }
  const auto* image = data.GetImage();
  if (image == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Associated image '{}' decoded to null payload",
                              name));
  }
  return std::move(*image->Clone());
}

aifocore::Result<ImageDimensions> MrxsAssociatedData::ReadImageDimensions(
    const fs::path& dirname, const SlideDataInfo& slide_info,
    std::string_view name) {
  AIFOCORE_ASSIGN_OR_RETURN(auto image, ReadImage(dirname, slide_info, name));
  return image.GetDimensions();
}

AssociatedDataType MrxsAssociatedData::DetectDataType(
    const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return AssociatedDataType::kUnknown;
  }

  if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
    return AssociatedDataType::kImage;  // JPEG
  }
  if (data.size() >= 4 && data[0] == 0x89 && data[1] == 0x50 &&
      data[2] == 0x4E && data[3] == 0x47) {
    return AssociatedDataType::kImage;  // PNG
  }
  if (data.size() >= 2 && data[0] == 0x42 && data[1] == 0x4D) {
    return AssociatedDataType::kImage;  // BMP
  }
  if (data.size() >= 5 && data[0] == '<' && data[1] == '?' && data[2] == 'x' &&
      data[3] == 'm' && data[4] == 'l') {
    return AssociatedDataType::kXml;
  }
  if (data.size() >= 3 && data[0] == 0x78 && data[1] == 0x9C) {
    return AssociatedDataType::kBinary;  // Zlib-compressed
  }

  // Heuristic: if a leading window is mostly printable ASCII, treat as text.
  if (data.size() >= 10) {
    int printable_count = 0;
    const size_t window = std::min(data.size(), static_cast<size_t>(100));
    for (size_t i = 0; i < window; ++i) {
      const uint8_t b = data[i];
      if ((b >= 32 && b <= 126) || b == '\n' || b == '\r' || b == '\t') {
        ++printable_count;
      }
    }
    if (printable_count > 90) {
      return AssociatedDataType::kXml;
    }
  }

  return AssociatedDataType::kBinary;
}

}  // namespace mrxs
}  // namespace fastslide
