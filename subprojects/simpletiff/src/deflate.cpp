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

#include "simpletiff/deflate.h"

#include <zlib.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "aifocore/status/result.h"

namespace simpletiff {

using ::aifocore::Result;
using ::aifocore::StatusCode;

namespace {

Result<void> InflateToVector(std::span<const uint8_t> compressed,
                             int window_bits, std::vector<uint8_t>& out) {
  out.clear();
  if (compressed.empty()) {
    return Result<void>();
  }

  z_stream strm{};
  strm.next_in = const_cast<Bytef*>(compressed.data());
  strm.avail_in = static_cast<uInt>(compressed.size());

  if (inflateInit2(&strm, window_bits) != Z_OK) {
    return AIFOCORE_MAKE_STATUS(StatusCode::kInternal,
                                "Deflate: inflateInit2 failed (window_bits=" +
                                    std::to_string(window_bits) + ")");
  }

  constexpr size_t kChunk = 256 * 1024;
  std::vector<uint8_t> chunk(kChunk);

  int ret = Z_OK;
  while (ret == Z_OK) {
    strm.next_out = chunk.data();
    strm.avail_out = static_cast<uInt>(chunk.size());
    ret = inflate(&strm, Z_NO_FLUSH);

    const size_t produced = chunk.size() - static_cast<size_t>(strm.avail_out);
    if (produced > 0) {
      out.insert(out.end(), chunk.data(), chunk.data() + produced);
    }
  }

  const std::string zlib_msg = (strm.msg != nullptr) ? strm.msg : "";
  inflateEnd(&strm);

  if (ret != Z_STREAM_END) {
    std::string msg =
        "Deflate: inflate failed (window_bits=" + std::to_string(window_bits) +
        ", zret=" + std::to_string(ret);
    if (!zlib_msg.empty()) {
      msg += ", zlib_msg=";
      msg += zlib_msg;
    }
    msg += ")";
    return AIFOCORE_MAKE_STATUS(StatusCode::kDataLoss, std::move(msg));
  }
  return Result<void>();
}

}  // namespace

Result<void> DecompressDeflate(std::span<const uint8_t> compressed,
                               std::vector<uint8_t>& out) {
  // 15 + 32 enables zlib's automatic header detection: it accepts both
  // zlib-wrapped (most common for TIFF Deflate/AdobeDeflate) and gzip-wrapped
  // streams (used by, e.g., Zarr V3's `gzip` codec).
  Result<void> auto_detect =
      InflateToVector(compressed, /*window_bits=*/15 + 32, out);
  if (auto_detect.ok()) {
    return Result<void>();
  }

  // Fallback: raw deflate stream (negative window bits) for headerless input.
  Result<void> raw = InflateToVector(compressed, /*window_bits=*/-15, out);
  if (raw.ok()) {
    return Result<void>();
  }

  return AIFOCORE_MAKE_STATUS(
      StatusCode::kDataLoss,
      "Deflate: failed in both auto (zlib/gzip) and raw modes [" +
          auto_detect.status().message() + "] [" + raw.status().message() +
          "]");
}

}  // namespace simpletiff
