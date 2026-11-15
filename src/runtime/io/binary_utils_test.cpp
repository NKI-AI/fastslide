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

std::vector<uint8_t> ZlibCompress(std::vector<uint8_t> data,
                                  int level = Z_BEST_SPEED) {
  const uLong src_len = static_cast<uLong>(data.size());
  uLongf dst_len = compressBound(src_len);
  std::vector<uint8_t> compressed(static_cast<size_t>(dst_len));

  const int ret =
      compress2(compressed.data(), &dst_len, data.data(), src_len, level);
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

TEST(HasZlibHeaderTest, AcceptsStreamsOfEveryCompressionLevel) {
  const std::string payload = "position position position position";
  const std::vector<uint8_t> original(payload.begin(), payload.end());
  for (int level = Z_NO_COMPRESSION; level <= Z_BEST_COMPRESSION; ++level) {
    EXPECT_TRUE(HasZlibHeader(ZlibCompress(original, level)))
        << "level " << level;
  }
}

// Only the first two bytes are fixed: 3DHISTECH position buffers start with
// 78 9C followed by whatever the deflate stream begins with.
TEST(HasZlibHeaderTest, IgnoresTheBytesAfterTheHeader) {
  EXPECT_TRUE(HasZlibHeader(std::vector<uint8_t>{0x78, 0x9C, 0xED}));
  EXPECT_TRUE(HasZlibHeader(std::vector<uint8_t>{0x78, 0x9C, 0xEC}));
  EXPECT_TRUE(HasZlibHeader(std::vector<uint8_t>{0x78, 0xDA}));
}

TEST(HasZlibHeaderTest, RejectsOtherData) {
  EXPECT_FALSE(HasZlibHeader(std::vector<uint8_t>{}));
  EXPECT_FALSE(HasZlibHeader(std::vector<uint8_t>{0x78}));
  // A raw MRXS position record starts with a 0/1 flag byte.
  EXPECT_FALSE(HasZlibHeader(std::vector<uint8_t>{0x00, 0x10, 0x00, 0x00}));
  EXPECT_FALSE(HasZlibHeader(std::vector<uint8_t>{0x01, 0x10, 0x00, 0x00}));
  // Wrong check bits.
  EXPECT_FALSE(HasZlibHeader(std::vector<uint8_t>{0x78, 0x9D}));
  // Not deflate (method 9), and a window above 32 KiB (CINFO 8), each with
  // valid check bits.
  EXPECT_FALSE(HasZlibHeader(std::vector<uint8_t>{0x79, 0x18}));
  EXPECT_FALSE(HasZlibHeader(std::vector<uint8_t>{0x88, 0x1C}));
}

}  // namespace
}  // namespace fastslide::runtime::io
