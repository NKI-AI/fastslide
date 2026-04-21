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

#include "simpletiff/zstd.h"

#include <zstd.h>

#include <string>
#include <vector>

#include "aifocore/status/result.h"

namespace simpletiff {

using ::aifocore::Result;
using ::aifocore::StatusCode;

Result<void> DecompressZstd(std::span<const uint8_t> compressed,
                            std::vector<uint8_t>& decompressed) {
  if (compressed.empty()) {
    return AIFOCORE_MAKE_STATUS(StatusCode::kInvalidArgument,
                                "ZSTD: empty compressed input");
  }

  // Get decompressed size from compressed frame
  const size_t decompressed_size =
      ZSTD_getFrameContentSize(compressed.data(), compressed.size());

  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
    return AIFOCORE_MAKE_STATUS(
        StatusCode::kInvalidArgument,
        "ZSTD: input is not a valid zstd-compressed frame");
  }

  if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return AIFOCORE_MAKE_STATUS(
        StatusCode::kInvalidArgument,
        "ZSTD: original (uncompressed) size is unknown in frame header");
  }

  decompressed.resize(static_cast<size_t>(decompressed_size));

  const size_t result =
      ZSTD_decompress(decompressed.data(), decompressed.size(),
                      compressed.data(), compressed.size());

  if (ZSTD_isError(result)) {
    return AIFOCORE_MAKE_STATUS(StatusCode::kDataLoss,
                                std::string("ZSTD: decompression failed: ") +
                                    ZSTD_getErrorName(result));
  }

  if (result != decompressed.size()) {
    return AIFOCORE_MAKE_STATUS(StatusCode::kDataLoss,
                                "ZSTD: decompressed size mismatch (expected " +
                                    std::to_string(decompressed.size()) +
                                    ", got " + std::to_string(result) + ")");
  }

  return Result<void>();
}

}  // namespace simpletiff
