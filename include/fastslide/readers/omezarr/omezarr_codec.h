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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_CODEC_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_CODEC_H_

#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/omezarr/omezarr_metadata.h"

namespace fastslide::formats::omezarr {

/// @brief Endianness for raw byte interpretation.
enum class Endian : uint8_t { kLittle, kBig };

/// @brief Decode a Zarr V3 codec chain into raw scalar bytes.
///
/// Supports the following codecs (in any order):
///   - "bytes" (a.k.a. "endian"): identity transform configuring endianness.
///   - "transpose": identity for the trivial "C-order" permutation, otherwise
///                  reorders chunk axes in place.
///   - "zstd": zstd decompression.
///   - "gzip": gzip / zlib decompression.
///   - "blosc": Blosc/Blosc2 decompression via c-blosc2.
///
/// Codecs are applied in reverse order (codec chain runs from the last entry
/// to the first when decoding). The expected uncompressed length, in bytes,
/// is required so that decompressors can size their output buffers.
class ZarrCodecChain {
 public:
  ZarrCodecChain() = default;

  /// @brief Build a codec chain from parsed Zarr V3 codec entries.
  static aifocore::Result<ZarrCodecChain> Build(
      const std::vector<ZarrCodec>& codecs,
      const std::vector<uint64_t>& chunk_shape, ZarrDtype dtype);

  /// @brief Decode `compressed` into a host-endian scalar byte buffer.
  ///
  /// The returned buffer has size `expected_size_bytes()`. The byte order
  /// matches the host machine (little-endian on x86/arm64), with no axis
  /// permutation applied.
  [[nodiscard]] aifocore::Result<std::vector<uint8_t>> Decode(
      std::span<const uint8_t> compressed) const;

  /// @brief Number of raw scalar bytes after decoding.
  [[nodiscard]] size_t expected_size_bytes() const noexcept {
    return expected_size_bytes_;
  }

  /// @brief Endianness recorded by the bytes/endian codec.
  [[nodiscard]] Endian endian() const noexcept { return endian_; }

 private:
  enum class Stage : uint8_t {
    kBytes,
    kTranspose,
    kZstd,
    kGzip,
    kBlosc,
  };

  std::vector<Stage> stages_;
  size_t expected_size_bytes_ = 0;
  uint32_t element_bytes_ = 1;
  Endian endian_ = Endian::kLittle;
};

}  // namespace fastslide::formats::omezarr

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_CODEC_H_
