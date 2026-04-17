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
// JPEG2000 decompression utilities (TIFF Compression=33003 or 33005)

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_JPEG2000_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_JPEG2000_H_

#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"

namespace simpletiff {

/// Decode a JPEG2000 (J2K codestream or JP2 file) buffer.
///
/// This supports TIFF Compression=33003 payloads as encountered in the wild.
/// The output is an interleaved pixel buffer with byte order matching the TIFF
/// file endianness (so the existing post-processing can apply endianness
/// normalization uniformly).
///
/// Limitations (by design, to keep SimpleTIFF "simple"):
/// - Only unsigned components are supported.
/// - Only byte-aligned sample sizes (8/16/32 bits) are supported.
/// - Only non-subsampled components (dx=dy=1, full resolution for all comps).
///
/// @param compressed JPEG2000 codestream (J2K) or container (JP2).
/// @param file_big_endian Whether the TIFF file is big-endian.
/// @param expected_bits_per_sample BitsPerSample from the TIFF page header.
/// @param expected_samples_per_pixel SamplesPerPixel from the TIFF page header.
/// @param convert_ycbcr_to_rgb If true, convert SYCC (YCbCr) to RGB output.
/// @param out_width Output decoded width in pixels.
/// @param out_height Output decoded height in pixels.
/// @param out Output pixel bytes (replaced).
/// @return Result indicating success or an error message.
aifocore::Result<void> DecodeJpeg2000(std::span<const uint8_t> compressed,
                                      bool file_big_endian,
                                      uint16_t expected_bits_per_sample,
                                      uint16_t expected_samples_per_pixel,
                                      bool convert_ycbcr_to_rgb, int& out_width,
                                      int& out_height,
                                      std::vector<uint8_t>& out);

}  // namespace simpletiff

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_JPEG2000_H_
