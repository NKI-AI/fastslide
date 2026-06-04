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

#include "fastslide/readers/omezarr/omezarr_codec.h"

#include <gtest/gtest.h>
#include <zlib.h>
#include <zstd.h>

#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

#include "fastslide/readers/omezarr/omezarr_metadata.h"

namespace fastslide::formats::omezarr {
namespace {

ZarrDtype Uint8Dtype() {
  ZarrDtype d;
  d.kind = ZarrDtypeKind::kUInt;
  d.bits = 8;
  return d;
}

ZarrDtype Uint16Dtype() {
  ZarrDtype d;
  d.kind = ZarrDtypeKind::kUInt;
  d.bits = 16;
  return d;
}

std::vector<uint8_t> ZstdCompress(std::span<const uint8_t> raw) {
  size_t bound = ZSTD_compressBound(raw.size());
  std::vector<uint8_t> compressed(bound);
  size_t written = ZSTD_compress(compressed.data(), compressed.size(),
                                 raw.data(), raw.size(), 1);
  EXPECT_FALSE(ZSTD_isError(written));
  compressed.resize(written);
  return compressed;
}

std::vector<uint8_t> GzipCompress(std::span<const uint8_t> raw) {
  z_stream zs{};
  EXPECT_EQ(deflateInit2(&zs, Z_BEST_SPEED, Z_DEFLATED, 15 + 16, 8,
                         Z_DEFAULT_STRATEGY),
            Z_OK);
  std::vector<uint8_t> out;
  out.resize(deflateBound(&zs, raw.size()));
  zs.next_in = const_cast<Bytef*>(raw.data());
  zs.avail_in = static_cast<uInt>(raw.size());
  zs.next_out = out.data();
  zs.avail_out = static_cast<uInt>(out.size());
  EXPECT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  return out;
}

TEST(OmezarrCodecTest, BytesIdentityPassthroughUint8) {
  std::vector<uint8_t> raw(256);
  std::iota(raw.begin(), raw.end(), 0);

  std::vector<ZarrCodec> codecs = {{"bytes", "{}"}};
  std::vector<uint64_t> chunk_shape = {16, 16};
  auto chain = ZarrCodecChain::Build(codecs, chunk_shape, Uint8Dtype());
  ASSERT_TRUE(chain.ok()) << chain.status().message();
  EXPECT_EQ(chain.value().expected_size_bytes(), raw.size());

  auto decoded = chain.value().Decode(raw);
  ASSERT_TRUE(decoded.ok()) << decoded.status().message();
  EXPECT_EQ(decoded.value(), raw);
}

TEST(OmezarrCodecTest, ZstdRoundTripUint8) {
  std::vector<uint8_t> raw(4096);
  for (size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<uint8_t>((i * 31 + 17) & 0xFF);
  }
  auto compressed = ZstdCompress(raw);

  std::vector<ZarrCodec> codecs = {{"bytes", "{}"}, {"zstd", "{}"}};
  std::vector<uint64_t> chunk_shape = {64, 64};
  auto chain = ZarrCodecChain::Build(codecs, chunk_shape, Uint8Dtype());
  ASSERT_TRUE(chain.ok()) << chain.status().message();
  EXPECT_EQ(chain.value().expected_size_bytes(), raw.size());

  auto decoded = chain.value().Decode(compressed);
  ASSERT_TRUE(decoded.ok()) << decoded.status().message();
  EXPECT_EQ(decoded.value(), raw);
}

TEST(OmezarrCodecTest, GzipRoundTripUint8) {
  std::vector<uint8_t> raw(2048);
  for (size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<uint8_t>((i * 7) & 0xFF);
  }
  auto compressed = GzipCompress(raw);

  std::vector<ZarrCodec> codecs = {{"bytes", "{}"}, {"gzip", "{}"}};
  std::vector<uint64_t> chunk_shape = {32, 64};
  auto chain = ZarrCodecChain::Build(codecs, chunk_shape, Uint8Dtype());
  ASSERT_TRUE(chain.ok()) << chain.status().message();

  auto decoded = chain.value().Decode(compressed);
  ASSERT_TRUE(decoded.ok()) << decoded.status().message();
  EXPECT_EQ(decoded.value(), raw);
}

TEST(OmezarrCodecTest, ExpectedBytesAccountForDtypeWidth) {
  std::vector<ZarrCodec> codecs = {{"bytes", "{}"}};
  std::vector<uint64_t> chunk_shape = {4, 8};
  auto chain = ZarrCodecChain::Build(codecs, chunk_shape, Uint16Dtype());
  ASSERT_TRUE(chain.ok());
  EXPECT_EQ(chain.value().expected_size_bytes(), 4u * 8u * 2u);
}

TEST(OmezarrCodecTest, RejectsUnknownCodec) {
  std::vector<ZarrCodec> codecs = {{"bytes", "{}"}, {"snazzy", "{}"}};
  std::vector<uint64_t> chunk_shape = {8, 8};
  auto chain = ZarrCodecChain::Build(codecs, chunk_shape, Uint8Dtype());
  EXPECT_FALSE(chain.ok());
}

}  // namespace
}  // namespace fastslide::formats::omezarr
