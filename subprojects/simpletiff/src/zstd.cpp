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

#include <cstdio>
#include <vector>

namespace simpletiff {

bool DecompressZstd(std::span<const uint8_t> compressed,
                    std::vector<uint8_t>& decompressed) {
  if (compressed.empty()) {
    return false;
  }

  // Get decompressed size from compressed frame
  const size_t decompressed_size =
      ZSTD_getFrameContentSize(compressed.data(), compressed.size());

  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
    std::fprintf(stderr, "Error: ZSTD not compressed by zstd\n");
    return false;
  }

  if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    std::fprintf(stderr, "Error: ZSTD original size unknown\n");
    return false;
  }

  // Allocate output buffer
  decompressed.resize(static_cast<size_t>(decompressed_size));

  // Decompress using simple API
  const size_t result =
      ZSTD_decompress(decompressed.data(), decompressed.size(),
                      compressed.data(), compressed.size());

  if (ZSTD_isError(result)) {
    std::fprintf(stderr, "Error: ZSTD decompression failed: %s\n",
                 ZSTD_getErrorName(result));
    return false;
  }

  // Verify size matches
  if (result != decompressed.size()) {
    std::fprintf(
        stderr,
        "Error: ZSTD decompressed size mismatch (expected %zu, got %zu)\n",
        decompressed.size(), result);
    return false;
  }

  return true;
}

}  // namespace simpletiff
