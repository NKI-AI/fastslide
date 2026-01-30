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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "gtest/gtest.h"

namespace fastslide::runtime::io {
namespace {

std::vector<uint8_t> ZlibCompress(std::vector<uint8_t> data) {
  const uLong src_len = static_cast<uLong>(data.size());
  uLongf dst_len = compressBound(src_len);
  std::vector<uint8_t> compressed(static_cast<size_t>(dst_len));

  const int ret = compress2(compressed.data(), &dst_len, data.data(), src_len,
                            Z_BEST_SPEED);
  EXPECT_EQ(ret, Z_OK);
  compressed.resize(static_cast<size_t>(dst_len));
  return compressed;
}

TEST(DecompressZlibTest, ResizesToActualOutput) {
  const std::string payload = "hello hello hello hello hello";
  std::vector<uint8_t> original(payload.begin(), payload.end());

  const std::vector<uint8_t> compressed = ZlibCompress(original);

  aifocore::Result<std::vector<uint8_t>> out_or =
      DecompressZlib(compressed.data(), compressed.size(), original.size() * 2);
  ASSERT_TRUE(out_or.ok()) << out_or.status().ToString();

  EXPECT_EQ(out_or->size(), original.size());
  EXPECT_EQ(*out_or, original);
}

TEST(DecompressZlibTest, FailsWhenOutputLimitTooSmall) {
  const std::string payload = "this should exceed the tiny output buffer";
  std::vector<uint8_t> original(payload.begin(), payload.end());

  const std::vector<uint8_t> compressed = ZlibCompress(original);

  aifocore::Result<std::vector<uint8_t>> out_or =
      DecompressZlib(compressed.data(), compressed.size(),
                     /*expected_size=*/original.size() / 2);
  ASSERT_FALSE(out_or.ok());
  EXPECT_EQ(out_or.status().code(), aifocore::StatusCode::kResourceExhausted);
}

TEST(DecompressZlibTest, WithActualSizeAllowsLargerThanHint) {
  const std::string payload = "hello hello hello hello hello";
  std::vector<uint8_t> original(payload.begin(), payload.end());

  const std::vector<uint8_t> compressed = ZlibCompress(original);

  aifocore::Result<ZlibDecompressionResult> out_or =
      DecompressZlibWithActualSize(compressed.data(), compressed.size(),
                                   /*expected_size_hint=*/1);
  ASSERT_TRUE(out_or.ok()) << out_or.status().ToString();

  EXPECT_EQ(out_or->actual_size_bytes, original.size());
  EXPECT_EQ(out_or->data, original);
}

}  // namespace
}  // namespace fastslide::runtime::io
