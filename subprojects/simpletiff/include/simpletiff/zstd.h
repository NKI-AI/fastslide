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
// ZSTD decompression for TIFF

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_ZSTD_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_ZSTD_H_

#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"

namespace simpletiff {

/// Decompress ZSTD compressed data.
///
/// @param compressed Compressed data
/// @param decompressed Output buffer (will be resized)
/// @return Ok status on success; an error status describing the failure
///         otherwise (empty input, missing frame size, decode error, ...).
::aifocore::Result<void> DecompressZstd(std::span<const uint8_t> compressed,
                                        std::vector<uint8_t>& decompressed);

}  // namespace simpletiff

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_ZSTD_H_
