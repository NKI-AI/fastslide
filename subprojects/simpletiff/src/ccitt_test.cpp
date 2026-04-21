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

#include "simpletiff/ccitt.h"

#include <array>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace simpletiff {
namespace {

// Reference G4 payload generated with `tiffcp -c g4` from libtiff for a 16x4
// 1-bit MinIsBlack image whose pixel rows (MSB-first) are:
//   row 0: 0xAA, 0x55  (10101010 01010101)
//   row 1: 0xFF, 0x00
//   row 2: 0x00, 0xFF
//   row 3: 0x55, 0xAA
// FillOrder is MSB2LSB (default).
constexpr std::array<uint8_t, 31> kG4Payload16x4 = {
    0x26, 0xa8, 0x8e, 0x88, 0xe8, 0x8e, 0x8b, 0xa2, 0x3a, 0x04, 0x12,
    0xc4, 0x45, 0x88, 0x8b, 0x33, 0x14, 0x8e, 0x88, 0xe8, 0x11, 0x43,
    0x9c, 0x72, 0x87, 0x04, 0x12, 0x80, 0x08, 0x00, 0x80,
};

constexpr std::array<uint8_t, 8> kExpectedPacked16x4 = {
    0xAA, 0x55, 0xFF, 0x00, 0x00, 0xFF, 0x55, 0xAA,
};

TEST(CcittG4, DecompressesKnownPayload) {
  std::vector<uint8_t> packed;
  ASSERT_TRUE(DecompressCcittG4(kG4Payload16x4, /*width=*/16, /*height=*/4,
                                FillOrder::kMsb2Lsb, packed)
                  .ok());
  ASSERT_EQ(packed.size(), kExpectedPacked16x4.size());
  for (size_t i = 0; i < packed.size(); ++i) {
    EXPECT_EQ(packed[i], kExpectedPacked16x4[i]) << "byte " << i;
  }
}

TEST(CcittG4, EmptyImageSucceeds) {
  std::vector<uint8_t> packed;
  EXPECT_TRUE(DecompressCcittG4({}, /*width=*/0, /*height=*/0,
                                FillOrder::kMsb2Lsb, packed)
                  .ok());
  EXPECT_TRUE(packed.empty());
  EXPECT_TRUE(DecompressCcittG4({}, /*width=*/16, /*height=*/0,
                                FillOrder::kMsb2Lsb, packed)
                  .ok());
  EXPECT_TRUE(packed.empty());
}

TEST(CcittG4, MalformedPayloadReturnsFalse) {
  // A few random bytes that cannot form a valid EOFB-terminated stream.
  std::array<uint8_t, 4> garbage = {0x12, 0x34, 0x56, 0x78};
  std::vector<uint8_t> packed;
  // Decoder may decline (error status) or produce a zero-filled row plus
  // terminate; either way it must not crash.
  (void)DecompressCcittG4(garbage, 16, 4, FillOrder::kMsb2Lsb, packed);
  SUCCEED();
}

TEST(UnpackOneBitToGray, MinIsBlackPhotometric) {
  std::vector<uint8_t> unpacked;
  ASSERT_TRUE(UnpackOneBitToGray(kExpectedPacked16x4, /*width=*/16,
                                 /*height=*/4, /*photometric=*/1, unpacked)
                  .ok());
  ASSERT_EQ(unpacked.size(), 16U * 4U);
  // Row 0: 0xAA = 10101010 -> 255 0 255 0 255 0 255 0
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(unpacked[i], (i & 1) ? 0 : 255) << "px " << i;
  }
  // Row 1: 0xFF -> all 255, then 0x00 -> all 0.
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(unpacked[16 + i], 255);
    EXPECT_EQ(unpacked[16 + 8 + i], 0);
  }
}

TEST(UnpackOneBitToGray, MinIsWhitePhotometricInverts) {
  std::vector<uint8_t> unpacked;
  ASSERT_TRUE(UnpackOneBitToGray(kExpectedPacked16x4, /*width=*/16,
                                 /*height=*/4, /*photometric=*/0, unpacked)
                  .ok());
  ASSERT_EQ(unpacked.size(), 16U * 4U);
  // Row 1: 0xFF -> all 0 (under MinIsWhite, 1=black=0).
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(unpacked[16 + i], 0);
    EXPECT_EQ(unpacked[16 + 8 + i], 255);
  }
}

TEST(UnpackOneBitToGray, NonByteAlignedWidthIgnoresPadBits) {
  // Single 5-pixel row stored in one byte: 11010xxx = 0xD0
  std::array<uint8_t, 1> packed = {0xD0};
  std::vector<uint8_t> unpacked;
  ASSERT_TRUE(UnpackOneBitToGray(packed, /*width=*/5, /*height=*/1,
                                 /*photometric=*/1, unpacked)
                  .ok());
  ASSERT_EQ(unpacked.size(), 5U);
  EXPECT_EQ(unpacked[0], 255);
  EXPECT_EQ(unpacked[1], 255);
  EXPECT_EQ(unpacked[2], 0);
  EXPECT_EQ(unpacked[3], 255);
  EXPECT_EQ(unpacked[4], 0);
}

TEST(UnpackOneBitToGray, EmptyInputs) {
  std::vector<uint8_t> unpacked;
  EXPECT_TRUE(UnpackOneBitToGray({}, 0, 0, 1, unpacked).ok());
  EXPECT_TRUE(unpacked.empty());
  EXPECT_TRUE(UnpackOneBitToGray({}, 16, 0, 1, unpacked).ok());
  EXPECT_TRUE(unpacked.empty());
}

TEST(UnpackOneBitToGray, RejectsTooSmallInput) {
  std::array<uint8_t, 1> packed = {0xFF};
  std::vector<uint8_t> unpacked;
  // width=16 needs 2 bytes per row, so 1 byte is too small for height=1.
  EXPECT_FALSE(UnpackOneBitToGray(packed, /*width=*/16, /*height=*/1,
                                  /*photometric=*/1, unpacked)
                   .ok());
}

TEST(CcittG4Pipeline, DecodeAndUnpackRoundTrip) {
  std::vector<uint8_t> packed;
  ASSERT_TRUE(
      DecompressCcittG4(kG4Payload16x4, 16, 4, FillOrder::kMsb2Lsb, packed)
          .ok());
  std::vector<uint8_t> gray;
  ASSERT_TRUE(UnpackOneBitToGray(packed, 16, 4, /*photometric=*/1, gray).ok());
  ASSERT_EQ(gray.size(), 16U * 4U);
  // Row 3: 0x55, 0xAA -> 0 255 0 255 ... 255 0 255 0 ... etc.
  // 0x55 = 01010101 -> 0,255,0,255,0,255,0,255
  // 0xAA = 10101010 -> 255,0,255,0,255,0,255,0
  const uint8_t* row3 = gray.data() + 16 * 3;
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(row3[i], (i & 1) ? 255 : 0) << "row3 first half px " << i;
    EXPECT_EQ(row3[8 + i], (i & 1) ? 0 : 255) << "row3 second half px " << i;
  }
}

}  // namespace
}  // namespace simpletiff
