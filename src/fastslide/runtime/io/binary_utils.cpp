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

#include "fastslide/runtime/io/binary_utils.h"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"

namespace fastslide {
namespace runtime {
namespace io {

absl::StatusOr<int32_t> ReadLeInt32(FILE* file) {
  uint8_t buf[4];
  if (fread(buf, 1, 4, file) != 4) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to read 4 bytes for int32");
  }
  // Little-endian byte order
  return static_cast<int32_t>(buf[0] | (buf[1] << 8) | (buf[2] << 16) |
                              (buf[3] << 24));
}

absl::StatusOr<uint32_t> ReadLeUInt32(FILE* file) {
  uint8_t buf[4];
  if (fread(buf, 1, 4, file) != 4) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to read 4 bytes for uint32");
  }
  // Little-endian byte order
  return static_cast<uint32_t>(buf[0] | (buf[1] << 8) | (buf[2] << 16) |
                               (buf[3] << 24));
}

absl::StatusOr<std::vector<uint8_t>> DecompressZlib(const uint8_t* data,
                                                    size_t compressed_size,
                                                    size_t expected_size) {
  std::vector<uint8_t> decompressed(expected_size);
  z_stream strm{};
  strm.next_in = const_cast<uint8_t*>(data);
  strm.avail_in = compressed_size;
  strm.next_out = decompressed.data();
  strm.avail_out = expected_size;

  if (inflateInit(&strm) != Z_OK) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to initialize zlib");
  }

  int ret = inflate(&strm, Z_FINISH);
  inflateEnd(&strm);

  if (ret != Z_STREAM_END) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        absl::StrFormat("Zlib decompression failed with error code: %d", ret));
  }

  return decompressed;
}

}  // namespace io
}  // namespace runtime
}  // namespace fastslide
