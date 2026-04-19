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
#include <vector>

namespace simpletiff {
namespace {

bool InflateToVector(std::span<const uint8_t> compressed, int window_bits,
                     std::vector<uint8_t>& out) {
  out.clear();
  if (compressed.empty()) {
    return true;
  }

  z_stream strm{};
  strm.next_in = const_cast<Bytef*>(compressed.data());
  strm.avail_in = static_cast<uInt>(compressed.size());

  if (inflateInit2(&strm, window_bits) != Z_OK) {
    return false;
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

  inflateEnd(&strm);
  return ret == Z_STREAM_END;
}

}  // namespace

bool DecompressDeflate(std::span<const uint8_t> compressed,
                       std::vector<uint8_t>& out) {
  // 15 = zlib header/trailer.
  if (InflateToVector(compressed, /*window_bits=*/15, out)) {
    return true;
  }
  // Fallback: raw deflate stream (negative window bits).
  return InflateToVector(compressed, /*window_bits=*/-15, out);
}

}  // namespace simpletiff
