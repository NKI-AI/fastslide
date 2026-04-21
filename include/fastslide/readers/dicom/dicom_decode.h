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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_DECODE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_DECODE_H_

/// @file dicom_decode.h
/// @brief Shared DICOM frame decoding helper.
///
/// Both the WSI tile executor and the associated-image reader need to turn a
/// raw libdicom frame payload into packed RGB8. This helper centralises the
/// per-transfer-syntax dispatch so neither path has to know the details.

#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/dicom/dicom.h"

namespace fastslide {
namespace dicom {
namespace internal {

/// @brief Decode a raw DICOM frame payload to packed RGB8.
///
/// Supports the same set of transfer syntaxes as the rest of the reader:
/// uncompressed Explicit VR Little Endian, JPEG Baseline, and both JPEG 2000
/// variants. For uncompressed frames the payload size is validated against
/// `expected_width * expected_height * 3`.
///
/// @param frame_bytes Raw frame payload as returned by libdicom.
/// @param syntax Transfer syntax used to encode the frame.
/// @param expected_width Frame width in pixels (Columns).
/// @param expected_height Frame height in pixels (Rows).
/// @return Decoded RGB8 buffer (width * height * 3 bytes) or an error status.
aifocore::Result<std::vector<uint8_t>> DecodeDicomFrameBytes(
    std::span<const uint8_t> frame_bytes, DicomTransferSyntax syntax,
    uint32_t expected_width, uint32_t expected_height);

}  // namespace internal
}  // namespace dicom
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_DECODE_H_
