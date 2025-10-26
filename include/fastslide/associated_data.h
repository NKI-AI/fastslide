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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_ASSOCIATED_DATA_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_ASSOCIATED_DATA_H_

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "fastslide/image.h"

namespace fastslide {

/// @brief Type of associated data
enum class AssociatedDataType {
  kImage,                ///< Image data (JPEG/PNG/BMP)
  kXml,                  ///< XML document
  kBinary,               ///< Generic binary data
  kPositionBuffer,       ///< Camera position buffer
  kIntensityCorrection,  ///< Intensity correction data
  kHistogram,            ///< Histogram data
  kUnknown,              ///< Unknown/unrecognized type
};

/// @brief Get string name for data type
inline const char* GetTypeName(AssociatedDataType type) {
  switch (type) {
    case AssociatedDataType::kImage:
      return "Image";
    case AssociatedDataType::kXml:
      return "XML";
    case AssociatedDataType::kBinary:
      return "Binary";
    case AssociatedDataType::kPositionBuffer:
      return "Position Buffer";
    case AssociatedDataType::kIntensityCorrection:
      return "Intensity Correction";
    case AssociatedDataType::kHistogram:
      return "Histogram";
    case AssociatedDataType::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

/// @brief Metadata about associated data
struct AssociatedDataInfo {
  std::string name;              ///< Data name/identifier
  AssociatedDataType type;       ///< Type of data
  std::string description;       ///< Human-readable description
  uint64_t size_bytes;           ///< Size in bytes (if known, 0 otherwise)
  bool is_compressed;            ///< True if data is compressed
  std::string compression_type;  ///< Compression type ("zlib", "none", etc.)
};

/// @brief Container for various types of associated data
struct AssociatedData {
  AssociatedDataInfo info;  ///< Metadata about the data

  /// @brief Actual data (one of these will be populated based on type)
  std::variant<RGBImage,             ///< Image data
               std::string,          ///< XML/text data
               std::vector<uint8_t>  ///< Binary data
               >
      data;

  /// @brief Check if this is an image
  [[nodiscard]] bool IsImage() const {
    return info.type == AssociatedDataType::kImage;
  }

  /// @brief Check if this is XML
  [[nodiscard]] bool IsXml() const {
    return info.type == AssociatedDataType::kXml;
  }

  /// @brief Check if this is binary
  [[nodiscard]] bool IsBinary() const {
    return info.type == AssociatedDataType::kBinary ||
           info.type == AssociatedDataType::kPositionBuffer ||
           info.type == AssociatedDataType::kIntensityCorrection ||
           info.type == AssociatedDataType::kHistogram;
  }

  /// @brief Get image (if this is an image)
  [[nodiscard]] const RGBImage* GetImage() const {
    return std::get_if<RGBImage>(&data);
  }

  /// @brief Get XML string (if this is XML)
  [[nodiscard]] const std::string* GetXml() const {
    return std::get_if<std::string>(&data);
  }

  /// @brief Get binary data (if this is binary)
  [[nodiscard]] const std::vector<uint8_t>* GetBinary() const {
    return std::get_if<std::vector<uint8_t>>(&data);
  }
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_ASSOCIATED_DATA_H_
