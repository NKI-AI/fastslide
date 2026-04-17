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
// TIFF compression and photometric interpretation type constants

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_TIFF_CONSTANTS_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_TIFF_CONSTANTS_H_

#include <cstdint>

namespace simpletiff {

/// TIFF compression type codes
///
/// These are the standard (and vendor-specific) compression codes used in TIFF
/// files. See TIFF 6.0 specification and LibTIFF documentation for details.
enum class Compression : uint16_t {
  kNone = 1,          ///< No compression
  kLzw = 5,           ///< LZW compression (Lempel-Ziv-Welch)
  kJpeg = 7,          ///< JPEG compression
  kAdobeDeflate = 8,  ///< Adobe Deflate (zlib/deflate), common in TIFF
  kDeflate = 32946,   ///< Deflate (same codec as AdobeDeflate)
  // JPEG 2000:
  // - 33003 is used by Aperio SVS for JP2K-compressed YCbCr tiles.
  // - 33005 is used by Aperio SVS for JP2K-compressed RGB tiles.
  //
  // SimpleTIFF treats both codes as JPEG2000 and routes them to the same
  // decoder. We keep the enum value at 33003 for historical reasons and rely
  // on IsCompression() to accept both. Use IsJpeg2000YCbCr() below to
  // distinguish the color space from the raw compression code.
  kJpeg2000 = 33003,
  kZstd = 50000,  ///< ZSTD compression (vendor-specific code)
};

/// Convert Compression enum to underlying uint16_t value
///
/// @param compression_value Compression enum value
/// @return Compression code as uint16_t
constexpr uint16_t ToCompressionCode(Compression compression_value) {
  return static_cast<uint16_t>(compression_value);
}

/// Check if a compression code matches a Compression enum value
///
/// @param code Compression code from TIFF file
/// @param expected Expected Compression enum value
/// @return True if codes match
constexpr bool IsCompression(uint16_t code, Compression expected) {
  if (expected == Compression::kJpeg2000) {
    return code == 33003u || code == 33005u;
  }
  return code == static_cast<uint16_t>(expected);
}

/// Determine whether a JPEG2000 compression code implies YCbCr color space.
///
/// Aperio SVS uses two vendor-specific compression codes for JP2K tiles:
///   - 33003: JP2K with YCbCr (sYCC) color space
///   - 33005: JP2K with RGB color space
///
/// The TIFF PhotometricInterpretation tag is unreliable for these files
/// (Aperio often writes Photometric=RGB even for YCbCr-encoded tiles).
/// OpenSlide uses the same convention to determine color space.
///
/// @param compression Raw compression code from the TIFF page header
/// @return true if the code indicates YCbCr data needing conversion to RGB
constexpr bool IsJpeg2000YCbCr(uint16_t compression) {
  return compression == 33003u;
}

/// TIFF photometric interpretation codes
///
/// Describes the color space of the image data.
/// See TIFF 6.0 specification for details.
enum class Photometric : uint8_t {
  kMinIsWhite = 0,        ///< Minimum value is white (grayscale)
  kMinIsBlack = 1,        ///< Minimum value is black (grayscale)
  kRgb = 2,               ///< RGB color space
  kPalette = 3,           ///< Palette/indexed color
  kTransparencyMask = 4,  ///< Transparency mask
  kCmyk = 5,              ///< CMYK color space
  kYCbCr = 6,             ///< YCbCr color space (often used with JPEG)
  kCieLab = 8,            ///< CIE L*a*b* color space
  kIccLab = 9,            ///< ICC L*a*b* color space
  kItuLab = 10,           ///< ITU L*a*b* color space
};

/// Convert Photometric enum to underlying uint16_t value
///
/// @param photometric_value Photometric enum value
/// @return Photometric code as uint16_t
constexpr uint16_t ToPhotometricCode(Photometric photometric_value) {
  return static_cast<uint16_t>(photometric_value);
}

/// Check if a photometric code matches a Photometric enum value
///
/// @param code Photometric code from TIFF file
/// @param expected Expected Photometric enum value
/// @return True if codes match
constexpr bool IsPhotometric(uint16_t code, Photometric expected) {
  return code == static_cast<uint16_t>(expected);
}

// =============================================================================
// BitsPerSample Validation and Conversion
// =============================================================================
//
// SimpleTIFF Design Decision: Byte-Aligned Formats Only
// -------------------------------------------------------
// SimpleTIFF supports only byte-aligned sample formats (8, 16, 32 bits/sample).
//
// Rationale:
// - Packed formats (1-bit, 4-bit, 12-bit) are valid TIFF but require complex
//   bit-unpacking logic that contradicts SimpleTIFF's "simple" design goal.
// - Byte-aligned formats cover 95%+ of real-world TIFFs (medical, WSI, photos).
// - Avoiding bit manipulation keeps the code maintainable and performant.
//
// If you need packed formats:
// - Use LibTIFF or another full-featured TIFF library
// - Pre-convert files to byte-aligned formats
// - Contribute unpacking support to SimpleTIFF
//
// The functions below explicitly validate and reject non-byte-aligned formats
// with clear error messages, avoiding silent failures from integer division.
// =============================================================================

/// Validate that bits_per_sample is byte-aligned and supported
///
/// SimpleTIFF only supports byte-aligned formats: 8, 16, and 32 bits/sample.
/// This explicitly excludes packed formats like 1-bit, 4-bit, and 12-bit.
///
/// @param bits_per_sample Bits per sample value from TIFF
/// @return true if supported (8, 16, 32), false otherwise
constexpr bool IsSupportedBitsPerSample(uint16_t bits_per_sample) {
  return bits_per_sample == 8 || bits_per_sample == 16 || bits_per_sample == 32;
}

/// Check if bits_per_sample is byte-aligned
///
/// @param bits_per_sample Bits per sample value from TIFF
/// @return true if evenly divisible by 8, false otherwise
constexpr bool IsByteAligned(uint16_t bits_per_sample) {
  return bits_per_sample > 0 && (bits_per_sample % 8) == 0;
}

/// Safely compute bytes per sample, returning 0 for unsupported values
///
/// This function prevents silent failures from integer division truncation.
/// Non-byte-aligned values (1-bit, 4-bit, 12-bit) explicitly return 0, which
/// calling code must check and handle with a clear error message.
///
/// Example:
/// @code
/// uint32_t bytes = ComputeBytesPerSample(page.bits_per_sample);
/// if (bytes == 0) {
///   fprintf(stderr, "Error: Unsupported bits_per_sample=%d\n",
///           page.bits_per_sample);
///   return false;
/// }
/// @endcode
///
/// @param bits_per_sample Bits per sample value from TIFF
/// @return Bytes per sample if byte-aligned, 0 if unsupported
constexpr uint32_t ComputeBytesPerSample(uint16_t bits_per_sample) {
  if (!IsByteAligned(bits_per_sample)) {
    return 0;  // Non-byte-aligned formats not supported
  }
  return static_cast<uint32_t>(bits_per_sample / 8);
}

}  // namespace simpletiff

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_TIFF_CONSTANTS_H_
