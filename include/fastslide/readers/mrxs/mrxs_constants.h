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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_CONSTANTS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_CONSTANTS_H_

#include <cstddef>
#include <cstdint>

/// @file mrxs_constants.h
/// @brief Constants and limits for MRXS format processing
///
/// Centralizes all magic numbers, file size limits, and format constants
/// used throughout the MRXS reader implementation. This eliminates magic
/// numbers scattered throughout the codebase and provides a single source
/// of truth for format-specific values.

namespace fastslide {
namespace mrxs {
namespace constants {

/// @brief Maximum allowed size for a single tile in bytes (100 MB)
///
/// Prevents bad_alloc from unreasonably large allocations when reading
/// malformed or corrupted MRXS files. Typical JPEG tiles are <10MB,
/// so 100MB provides a generous safety margin.
constexpr int64_t kMaxTileSize = 100 * 1024 * 1024;

/// @brief Threshold percentage for detecting text/XML content (90%)
///
/// Used when analyzing binary data to determine if it contains printable
/// text or XML. If more than 90% of sampled bytes are printable ASCII
/// characters, the data is classified as text rather than binary.
constexpr int kPrintableThreshold = 90;

/// @brief Estimated maximum compression ratio for zlib (100x)
///
/// Used to estimate decompressed size when actual size is unknown.
/// Provides a conservative upper bound for allocating decompression buffers.
constexpr int kCompressionRatioEstimate = 100;

/// @brief Size of each camera position record in bytes
///
/// Camera position format (9 bytes total):
/// - 1 byte: flag (typically 0 or 1)
/// - 4 bytes: x coordinate (little-endian int32)
/// - 4 bytes: y coordinate (little-endian int32)
constexpr size_t kPositionRecordSize = 9;

/// @brief Number of bytes to sample when detecting text content
///
/// When analyzing unknown data to determine if it's text or binary,
/// we examine the first 100 bytes for printable character ratio.
constexpr size_t kTextDetectionSampleSize = 100;

/// @brief MRXS index file version string
///
/// All MRXS index files start with this 5-byte version identifier.
constexpr const char* kIndexVersion = "01.02";

/// @brief Size of MRXS index file version field in bytes
constexpr size_t kIndexVersionSize = 5;

}  // namespace constants
}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_CONSTANTS_H_
