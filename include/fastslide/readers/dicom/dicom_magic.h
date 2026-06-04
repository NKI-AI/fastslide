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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_MAGIC_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_MAGIC_H_

#include <cstddef>
#include <filesystem>
#include <span>

namespace fastslide::dicom {

/// @brief Byte offset of the "DICM" magic in a Part 10 DICOM file.
///
/// Per DICOM PS3.10 Section 7, every Part 10 conformant file starts with a
/// 128-byte preamble (typically zero-filled) followed by the four ASCII
/// bytes "DICM" beginning at offset 128.
inline constexpr std::size_t kDicomMagicOffset = 128;

/// @brief Length of the DICOM magic ("DICM"), in bytes.
inline constexpr std::size_t kDicomMagicLength = 4;

/// @brief Total size of the DICOM file preamble plus magic, in bytes.
inline constexpr std::size_t kDicomMagicHeaderSize =
    kDicomMagicOffset + kDicomMagicLength;

/// @brief Check whether a buffer starts with a valid DICOM Part 10 header.
///
/// @param buffer Bytes from the start of the file. Must be at least
///        @ref kDicomMagicHeaderSize bytes long for a positive match.
/// @return @c true iff bytes 128..131 equal the ASCII string "DICM".
[[nodiscard]] bool BufferHasDicomMagic(std::span<const unsigned char> buffer);

/// @brief Check whether a file on disk is a Part 10 DICOM file.
///
/// Reads the first @ref kDicomMagicHeaderSize bytes of @p path and checks
/// the magic. Returns @c false on any I/O error or for files that are too
/// short. Never throws.
///
/// @param path Path to a regular file.
/// @return @c true if the file appears to be a Part 10 DICOM file.
[[nodiscard]] bool HasDicomMagic(const std::filesystem::path& path);

}  // namespace fastslide::dicom

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_MAGIC_H_
