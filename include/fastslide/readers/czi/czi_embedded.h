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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_EMBEDDED_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_EMBEDDED_H_

/// @file czi_embedded.h
/// @brief Helpers for parsing single-subblock "embedded CZI" attachments.
///
/// CZI attachments may either be standalone JPEGs or fully-formed embedded
/// CZIs containing exactly one subblock (used for label, slide preview, and
/// thumbnail images). The helpers here parse the embedded directory header
/// and decode the payload (uncompressed BGR or zstd/zstd1-compressed BGR/16)
/// to a packed RGB8 buffer.

#include <cstdint>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/runtime/io/file_reader.h"

namespace fastslide {
namespace czi {
namespace internal {

/// @brief Metadata for the single subblock contained in an embedded CZI.
struct EmbeddedSubblock {
  int64_t file_pos = 0;     ///< Subblock segment offset relative to base.
  int32_t pixel_type = 0;   ///< CZI pixel type (3 = BGR24, 4 = BGR48).
  int32_t compression = 0;  ///< CZI compression code (0/5/6 supported).
  int32_t x = 0;            ///< X position of the subblock.
  int32_t y = 0;            ///< Y position of the subblock.
  uint32_t w = 0;           ///< Stored width in pixels.
  uint32_t h = 0;           ///< Stored height in pixels.
};

/// @brief Parse the directory of an embedded CZI and return its single entry.
///
/// Reads the embedded ZISRAWFILE header at `base_offset`, validates the
/// directory contains exactly one entry, and returns its dimensions and
/// compression metadata.
///
/// @param file Open file reader positioned anywhere; will be re-seeked.
/// @param base_offset Absolute file offset of the embedded ZISRAWFILE header.
/// @return Subblock metadata or an error status.
aifocore::Result<EmbeddedSubblock> ParseEmbeddedSingleSubblock(
    FileReader& file, int64_t base_offset);

/// @brief Decode an embedded subblock payload into packed RGB8 bytes.
///
/// Supports BGR24 (pixel_type 3) and BGR48 (pixel_type 4) with optional
/// zstd (compression 5) or zstd1 (compression 6) compression. BGR48 values
/// are normalised against the per-image maximum to avoid extremely dark
/// output from instruments that store low-bit samples in 16-bit channels.
///
/// @param file Open file reader positioned anywhere; will be re-seeked.
/// @param base_offset Absolute file offset of the embedded ZISRAWFILE header.
/// @param sb Subblock metadata previously returned by
///   `ParseEmbeddedSingleSubblock`.
/// @return RGB8 pixel buffer (row-major, sb.w * sb.h * 3 bytes) or error.
aifocore::Result<std::vector<uint8_t>> ReadEmbeddedSubblockRgb8(
    FileReader& file, int64_t base_offset, const EmbeddedSubblock& sb);

}  // namespace internal
}  // namespace czi
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_EMBEDDED_H_
