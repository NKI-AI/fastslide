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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <vector>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {
namespace runtime {
namespace io {

namespace {

constexpr size_t kMinOutputChunkSizeBytes = 16 * 1024;

aifocore::Result<ZlibDecompressionResult> DecompressZlibImpl(
    const uint8_t* data, size_t compressed_size, size_t expected_size_hint,
    std::optional<size_t> max_output_size_bytes) {
  if (compressed_size > static_cast<size_t>(std::numeric_limits<uInt>::max())) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Compressed zlib payload too large: {} bytes",
                              compressed_size));
  }

  if (max_output_size_bytes.has_value() &&
      *max_output_size_bytes >
          static_cast<size_t>(std::numeric_limits<uInt>::max())) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Max zlib output too large: {} bytes",
                              *max_output_size_bytes));
  }

  z_stream strm{};
  strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
  strm.avail_in = static_cast<uInt>(compressed_size);

  if (inflateInit(&strm) != Z_OK) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to initialize zlib");
  }

  // Special case: if the maximum output size is 0, any non-empty decompressed
  // output must be reported as ResourceExhausted, but we still need to probe
  // the stream to see whether it expands to empty or not.
  if (max_output_size_bytes.has_value() && *max_output_size_bytes == 0) {
    uint8_t probe_buf[1];
    strm.next_out = probe_buf;
    strm.avail_out = 1;

    const int ret = inflate(&strm, Z_NO_FLUSH);
    inflateEnd(&strm);

    if (ret == Z_STREAM_END && strm.total_out == 0) {
      return ZlibDecompressionResult{.data = {}, .actual_size_bytes = 0};
    }
    if (ret == Z_OK || ret == Z_STREAM_END || ret == Z_BUF_ERROR) {
      return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                              "Zlib decompression output exceeds limit");
    }
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Zlib decompression failed with error code: {}",
                              ret));
  }

  std::vector<uint8_t> out;
  if (expected_size_hint > 0) {
    out.resize(expected_size_hint);
  } else {
    out.resize(kMinOutputChunkSizeBytes);
  }

  while (true) {
    const size_t already_written = static_cast<size_t>(strm.total_out);
    if (already_written > out.size()) {
      inflateEnd(&strm);
      return aifocore::Status(aifocore::StatusCode::kInternal,
                              "Zlib decompressor wrote past output buffer");
    }

    if (already_written == out.size()) {
      size_t new_size = out.size();
      if (new_size < kMinOutputChunkSizeBytes) {
        new_size = kMinOutputChunkSizeBytes;
      }
      new_size = std::max(new_size * 2, out.size() + kMinOutputChunkSizeBytes);

      if (max_output_size_bytes.has_value()) {
        if (out.size() >= *max_output_size_bytes) {
          inflateEnd(&strm);
          return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                                  "Zlib decompression output exceeds limit");
        }
        new_size = std::min(new_size, *max_output_size_bytes);
      }

      out.resize(new_size);
    }

    const size_t avail_out = out.size() - already_written;
    if (avail_out > static_cast<size_t>(std::numeric_limits<uInt>::max())) {
      inflateEnd(&strm);
      return aifocore::Status(
          aifocore::StatusCode::kInvalidArgument,
          "Zlib decompression output chunk too large for zlib");
    }

    strm.next_out = out.data() + already_written;
    strm.avail_out = static_cast<uInt>(avail_out);

    const int ret = inflate(&strm, Z_NO_FLUSH);
    if (ret == Z_STREAM_END) {
      break;
    }
    if (ret == Z_OK) {
      continue;
    }

    // Z_BUF_ERROR can happen when inflate makes no progress; if the output
    // buffer is full, we'll grow it and continue.
    if (ret == Z_BUF_ERROR && strm.avail_out == 0) {
      continue;
    }

    inflateEnd(&strm);
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Zlib decompression failed with error code: {}",
                              ret));
  }

  const size_t actual_size = static_cast<size_t>(strm.total_out);
  inflateEnd(&strm);

  if (actual_size > out.size()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Zlib decompression produced invalid size");
  }

  out.resize(actual_size);
  return ZlibDecompressionResult{.data = std::move(out),
                                 .actual_size_bytes = actual_size};
}

}  // namespace

aifocore::Result<int32_t> ReadLeInt32(FILE* file) {
  uint8_t buf[4];
  if (aifocore::portable_fread(buf, sizeof(buf), file) != sizeof(buf)) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to read 4 bytes for int32");
  }
  // Little-endian byte order
  return static_cast<int32_t>(buf[0] | (buf[1] << 8) | (buf[2] << 16) |
                              (buf[3] << 24));
}

aifocore::Result<uint32_t> ReadLeUInt32(FILE* file) {
  uint8_t buf[4];
  if (aifocore::portable_fread(buf, sizeof(buf), file) != sizeof(buf)) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to read 4 bytes for uint32");
  }
  // Little-endian byte order
  return static_cast<uint32_t>(buf[0] | (buf[1] << 8) | (buf[2] << 16) |
                               (buf[3] << 24));
}

aifocore::Result<uint64_t> ReadLeUInt64(FILE* file) {
  uint8_t buf[8];
  if (aifocore::portable_fread(buf, sizeof(buf), file) != sizeof(buf)) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to read 8 bytes for uint64");
  }
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= (static_cast<uint64_t>(buf[i]) << (8 * i));
  }
  return v;
}

aifocore::Result<int64_t> ReadLeInt64(FILE* file) {
  AIFOCORE_ASSIGN_OR_RETURN(const uint64_t u, ReadLeUInt64(file));
  return static_cast<int64_t>(u);
}

aifocore::Result<std::vector<uint8_t>> DecompressZlib(const uint8_t* data,
                                                      size_t compressed_size,
                                                      size_t expected_size) {
  AIFOCORE_ASSIGN_OR_RETURN(
      ZlibDecompressionResult out,
      DecompressZlibImpl(data, compressed_size,
                         /*expected_size_hint=*/expected_size,
                         /*max_output_size_bytes=*/expected_size));
  return std::move(out.data);
}

aifocore::Result<ZlibDecompressionResult> DecompressZlibWithActualSize(
    const uint8_t* data, size_t compressed_size, size_t expected_size_hint) {
  return DecompressZlibImpl(data, compressed_size, expected_size_hint,
                            /*max_output_size_bytes=*/std::nullopt);
}

}  // namespace io
}  // namespace runtime
}  // namespace fastslide
