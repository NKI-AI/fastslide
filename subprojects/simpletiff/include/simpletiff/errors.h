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
// Error types for SimpleTIFF library

#ifndef SIMPLETIFF_ERRORS_H_
#define SIMPLETIFF_ERRORS_H_

#include <cstdint>
#include <stdexcept>
#include <string>

namespace simpletiff {

/// Base exception for all SimpleTIFF errors
class TiffError : public std::runtime_error {
 public:
  explicit TiffError(const std::string& message)
      : std::runtime_error(message) {}
};

/// Unsupported feature or format
class UnsupportedFormatError : public TiffError {
 public:
  explicit UnsupportedFormatError(const std::string& message)
      : TiffError(message) {}

  /// Create error for unsupported compression
  static UnsupportedFormatError Compression(uint16_t compression_code,
                                            uint32_t page_index) {
    return UnsupportedFormatError("Unsupported compression scheme " +
                                  std::to_string(compression_code) +
                                  " on page " + std::to_string(page_index));
  }

  /// Create error for unsupported bits per sample
  static UnsupportedFormatError BitsPerSample(uint16_t bits_per_sample,
                                              uint32_t page_index) {
    return UnsupportedFormatError(
        "Unsupported bits_per_sample=" + std::to_string(bits_per_sample) +
        " on page " + std::to_string(page_index) +
        ". SimpleTIFF requires byte-aligned formats (8, 16, or 32 bits)");
  }
};

/// Decompression failure
class DecompressionError : public TiffError {
 public:
  explicit DecompressionError(const std::string& message)
      : TiffError(message) {}

  /// Create error for specific codec failure
  static DecompressionError Codec(const std::string& codec_name,
                                  uint32_t page_index,
                                  uint32_t tile_or_strip_index) {
    return DecompressionError(codec_name + " decompression failed for page " +
                              std::to_string(page_index) + " tile/strip " +
                              std::to_string(tile_or_strip_index));
  }
};

/// Invalid page parameters
class InvalidPageError : public TiffError {
 public:
  explicit InvalidPageError(const std::string& message) : TiffError(message) {}

  /// Create error for invalid image parameters
  static InvalidPageError Parameters(uint32_t page_index,
                                     uint32_t samples_per_pixel,
                                     uint32_t bytes_per_sample) {
    return InvalidPageError(
        "Invalid image parameters for page " + std::to_string(page_index) +
        " (samples_per_pixel=" + std::to_string(samples_per_pixel) +
        ", bytes_per_sample=" + std::to_string(bytes_per_sample) + ")");
  }

  /// Create error for invalid storage type
  static InvalidPageError Storage(uint32_t page_index,
                                  const std::string& expected,
                                  const std::string& actual) {
    return InvalidPageError("Page " + std::to_string(page_index) +
                            " storage mismatch: expected " + expected +
                            ", got " + actual);
  }
};

/// Out of bounds access
class IndexError : public TiffError {
 public:
  explicit IndexError(const std::string& message) : TiffError(message) {}

  /// Create error for invalid page index
  static IndexError PageIndex(uint32_t page_index, uint32_t num_pages) {
    return IndexError("Page index " + std::to_string(page_index) +
                      " out of range (file has " + std::to_string(num_pages) +
                      " pages)");
  }

  /// Create error for invalid tile/strip index
  static IndexError TileIndex(uint32_t tile_index, uint32_t num_tiles,
                              uint32_t page_index) {
    return IndexError("Tile index " + std::to_string(tile_index) +
                      " out of range on page " + std::to_string(page_index) +
                      " (has " + std::to_string(num_tiles) + " tiles)");
  }
};

}  // namespace simpletiff

#endif  // SIMPLETIFF_ERRORS_H_
