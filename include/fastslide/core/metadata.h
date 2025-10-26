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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_METADATA_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_METADATA_H_

#include <map>
#include <string>
#include <string_view>
#include <variant>

/**
 * @file metadata.h
 * @brief Core metadata container without external dependencies
 * 
 * This header defines the metadata container and standardized keys for
 * slide metadata. This is a pure domain model with minimal dependencies,
 * suitable for use in language bindings and format plugins.
 */

namespace fastslide {
namespace core {

/// @brief Metadata key constants for standardized access
///
/// This namespace defines the standardized metadata keys that slide readers
/// should populate. Keys are categorized as:
/// - **Mandatory**: All readers MUST provide these keys
/// - **Optional**: Readers SHOULD provide these if available
/// - **Format-specific**: Readers MAY provide additional format-specific keys
namespace MetadataKeys {

// Mandatory keys (all readers must provide)
inline constexpr std::string_view kFormat =
    "format";  ///< File format name (e.g., "MRXS", "SVS", "QPTIFF")
inline constexpr std::string_view kLevels =
    "levels";  ///< Number of pyramid levels

// Optional keys (provide if available)
inline constexpr std::string_view kMppX =
    "mpp_x";  ///< Microns per pixel in X direction
inline constexpr std::string_view kMppY =
    "mpp_y";  ///< Microns per pixel in Y direction
inline constexpr std::string_view kMagnification =
    "magnification";  ///< Objective magnification
inline constexpr std::string_view kObjective = "objective";  ///< Objective name
inline constexpr std::string_view kScannerModel =
    "scanner_model";  ///< Scanner manufacturer/model
inline constexpr std::string_view kScannerID = "scanner_id";  ///< Scanner ID
inline constexpr std::string_view kSlideID = "slide_id";  ///< Slide identifier
inline constexpr std::string_view kChannels =
    "channels";  ///< Number of channels
inline constexpr std::string_view kAssociatedImages =
    "associated_images";  ///< Number of associated images

// Format-specific keys (readers may add more as needed)

}  // namespace MetadataKeys

/// @brief Metadata value type
///
/// Metadata values can be strings, integers (size_t), or floating-point
/// numbers (double). This variant provides type-safe storage.
using MetadataValue = std::variant<std::string, size_t, double>;

/// @brief Lightweight metadata container
///
/// This is a thin wrapper around std::map that provides a standardized
/// interface for slide metadata. It's designed to be lightweight and
/// have minimal dependencies.
///
/// Note: For rich functionality (printing, validation, type conversion),
/// use the full Metadata class in fastslide/metadata.h instead.
using MetadataContainer = std::map<std::string, MetadataValue>;

}  // namespace core

// Import core types into fastslide namespace
using core::MetadataContainer;
using core::MetadataValue;
// Note: MetadataKeys is imported via metadata.h (not needed here)

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_METADATA_H_
