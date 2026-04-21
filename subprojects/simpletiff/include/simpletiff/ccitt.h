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
//
// CCITT bilevel (T.4 / T.6 / fax) decompression for TIFF.
//
// Implementation derived from libtiff's tif_fax3.c by Sam Leffler /
// Silicon Graphics, Inc. The original BSD-style license is preserved in the
// implementation files (ccitt.cpp, ccitt_tables.cpp).

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_CCITT_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_CCITT_H_

#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"

namespace simpletiff {

/// TIFF FillOrder tag values (TIFF 6.0 spec, tag 266).
enum class FillOrder : uint8_t {
  kMsb2Lsb = 1,  ///< Most significant bit first (default)
  kLsb2Msb = 2,  ///< Least significant bit first
};

/// Decompress a CCITT Group 4 (T.6) bilevel image.
///
/// Produces packed 1-bit rows: each row contains ((width + 7) / 8) bytes,
/// MSB-first within each byte. Bit value 1 represents black, 0 white
/// (per the TIFF baseline convention; PhotometricInterpretation handling
/// is the caller's responsibility, e.g. via UnpackOneBitToGray).
///
/// @param compressed Compressed CCITT G4 bytes
/// @param width Row width in pixels
/// @param height Number of rows to decode
/// @param fill_order TIFF FillOrder tag value (defaults to MSB2LSB)
/// @param decompressed Output buffer (will be resized to height * row_bytes)
/// @return Ok status on success; an error status on irrecoverable decoder
///         error (e.g. allocation failure, invalid stream).
::aifocore::Result<void> DecompressCcittG4(std::span<const uint8_t> compressed,
                                           uint32_t width, uint32_t height,
                                           FillOrder fill_order,
                                           std::vector<uint8_t>& decompressed);

/// Decompress a CCITT Group 3 (T.4 1-D) modified-Huffman bilevel image.
///
/// Same output convention as DecompressCcittG4. This handles compression
/// codes 2 (CCITT modified Huffman RLE) and 3 (CCITT Group 3 fax 1-D). The
/// 2-D variant of T.4 is not currently exposed.
///
/// @param compressed Compressed CCITT G3/RLE bytes
/// @param width Row width in pixels
/// @param height Number of rows to decode
/// @param fill_order TIFF FillOrder tag value
/// @param decompressed Output buffer (will be resized to height * row_bytes)
/// @return Ok status on success; an error status on irrecoverable decoder
///         error (e.g. allocation failure, invalid stream).
::aifocore::Result<void> DecompressCcittG3(std::span<const uint8_t> compressed,
                                           uint32_t width, uint32_t height,
                                           FillOrder fill_order,
                                           std::vector<uint8_t>& decompressed);

/// Unpack packed 1-bit rows to 8-bit grayscale.
///
/// Each input bit becomes one output byte. Photometric interpretation is
/// honored: for kMinIsBlack, bit 1 -> 255 and bit 0 -> 0; for kMinIsWhite
/// the mapping is inverted. Other photometrics are treated as MinIsBlack.
///
/// Each input row is assumed to occupy ((width + 7) / 8) bytes (MSB-first).
/// Trailing pad bits within the last byte of a row are ignored.
///
/// @param packed Packed 1-bit input, height * row_bytes_packed bytes
/// @param width Row width in pixels
/// @param height Number of rows
/// @param photometric TIFF PhotometricInterpretation tag value (0=MinIsWhite,
///                    1=MinIsBlack)
/// @param unpacked Output buffer (will be resized to height * width bytes)
/// @return Ok status on success; an error status on input size mismatch.
::aifocore::Result<void> UnpackOneBitToGray(std::span<const uint8_t> packed,
                                            uint32_t width, uint32_t height,
                                            uint16_t photometric,
                                            std::vector<uint8_t>& unpacked);

}  // namespace simpletiff

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_CCITT_H_
