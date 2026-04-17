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
#include <vector>

#include "gtest/gtest.h"

namespace simpletiff {
namespace {

std::vector<uint8_t> ZlibCompress(std::span<const uint8_t> src) {
  z_stream strm{};
  if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
    return {};
  }

  std::vector<uint8_t> out;
  out.resize(deflateBound(&strm, static_cast<uLong>(src.size())));

  strm.next_in = const_cast<Bytef*>(src.data());
  strm.avail_in = static_cast<uInt>(src.size());
  strm.next_out = out.data();
  strm.avail_out = static_cast<uInt>(out.size());

  const int ret = deflate(&strm, Z_FINISH);
  deflateEnd(&strm);
  if (ret != Z_STREAM_END) {
    return {};
  }

  out.resize(out.size() - static_cast<size_t>(strm.avail_out));
  return out;
}

TEST(DeflateTest, ZlibWrappedStreamRoundtrip) {
  std::vector<uint8_t> original;
  original.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    original.push_back(static_cast<uint8_t>((i * 31) & 0xFF));
  }

  const std::vector<uint8_t> compressed = ZlibCompress(original);
  ASSERT_FALSE(compressed.empty());

  std::vector<uint8_t> decompressed;
  ASSERT_TRUE(DecompressDeflate(compressed, decompressed));
  EXPECT_EQ(decompressed, original);
}

}  // namespace
}  // namespace simpletiff
