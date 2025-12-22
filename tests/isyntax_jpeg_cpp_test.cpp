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

#include "fastslide/readers/isyntax/third_party/jpeg.h"
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(IsyntaxJpegCppTest, BgraToRgbaSwizzle) {
  // One pixel: BGRA = 0xAARRGGBB in little endian? Here we treat uint32_t as
  // packed bytes in memory. We validate by byte extraction.
  // Build BGRA bytes: B=0x11, G=0x22, R=0x33, A=0x44.
  uint32_t pixel = 0;
  reinterpret_cast<uint8_t*>(&pixel)[0] = 0x11;
  reinterpret_cast<uint8_t*>(&pixel)[1] = 0x22;
  reinterpret_cast<uint8_t*>(&pixel)[2] = 0x33;
  reinterpret_cast<uint8_t*>(&pixel)[3] = 0x44;

  isyntax::jpeg::SwapRedBlueInPlace(std::span<uint32_t>(&pixel, 1),
                                    /*width=*/1, /*height=*/1);

  const auto* bytes = reinterpret_cast<const uint8_t*>(&pixel);
  EXPECT_EQ(bytes[0], 0x33);  // R
  EXPECT_EQ(bytes[1], 0x22);  // G
  EXPECT_EQ(bytes[2], 0x11);  // B
  EXPECT_EQ(bytes[3], 0x44);  // A
}

TEST(IsyntaxJpegCppTest, ReadAssociatedImageJpegBytesInvalidArgs) {
  auto res = isyntax::jpeg::ReadAssociatedImageJpegBytes(nullptr, nullptr);
  EXPECT_FALSE(res.ok());
}

TEST(IsyntaxJpegCppTest, ReadIccProfileBytesInvalidArgs) {
  auto res = isyntax::jpeg::ReadIccProfileBytes(nullptr, nullptr);
  EXPECT_FALSE(res.ok());
}

}  // namespace
