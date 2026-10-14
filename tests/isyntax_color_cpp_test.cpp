// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#include "fastslide/readers/isyntax/third_party/color.h"

#include <array>
#include <cstdint>
#include <random>

#include <gtest/gtest.h>

namespace isyntax {
namespace color {
namespace {

// Odd negative chroma is where `/ 2` (truncation) and `>> 1` (floor) differ;
// the Philips SDK floors.
TEST(IsyntaxColorTest, HalvingRoundsTowardNegativeInfinity) {
  const rgba_t px = YCoCgToRgb(100, -9, -9);
  EXPECT_EQ(px.r, 101);
  EXPECT_EQ(px.g, 96);
  EXPECT_EQ(px.b, 110);
}

TEST(IsyntaxColorTest, ClampsBothEnds) {
  const rgba_t low = YCoCgToRgb(5, -40, 0);  // R = -15
  EXPECT_EQ(low.r, 0);
  const rgba_t high = YCoCgToRgb(250, 40, 0);  // R = 270
  EXPECT_EQ(high.r, 255);

  const rgba_t bgr = YCoCgToBgr(5, -40, 0);
  EXPECT_EQ(bgr.b, 0);  // R lands in the third byte
}

// The block conversion runs SIMD on whole groups of 8 pixels and the scalar
// functions on the rest of the row: both must give the same pixels.
TEST(IsyntaxColorTest, BlockConversionMatchesScalar) {
  constexpr int32_t kWidth = 13;
  constexpr int32_t kHeight = 4;
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> luma(-20, 280);
  std::uniform_int_distribution<int> chroma(-120, 120);
  std::array<int16_t, kWidth * kHeight> y{}, co{}, cg{};
  for (int32_t i = 0; i < kWidth * kHeight; ++i) {
    y[i] = static_cast<int16_t>(luma(rng));
    co[i] = static_cast<int16_t>(chroma(rng));
    cg[i] = static_cast<int16_t>(chroma(rng));
  }

  std::array<uint32_t, kWidth * kHeight> rgba{}, bgra{};
  ConvertYCoCgToRgbaBlock(y.data(), co.data(), cg.data(), kWidth, kHeight,
                          kWidth, rgba.data());
  ConvertYCoCgToBgraBlock(y.data(), co.data(), cg.data(), kWidth, kHeight,
                          kWidth, bgra.data());

  for (int32_t i = 0; i < kWidth * kHeight; ++i) {
    const rgba_t expected_rgba = YCoCgToRgb(y[i], co[i], cg[i]);
    const rgba_t expected_bgra = YCoCgToBgr(y[i], co[i], cg[i]);
    const auto* got_rgba = reinterpret_cast<const rgba_t*>(&rgba[i]);
    const auto* got_bgra = reinterpret_cast<const rgba_t*>(&bgra[i]);
    for (int c = 0; c < 4; ++c) {
      EXPECT_EQ(got_rgba->values[c], expected_rgba.values[c]) << "pixel " << i;
      EXPECT_EQ(got_bgra->values[c], expected_bgra.values[c]) << "pixel " << i;
    }
  }
}

}  // namespace
}  // namespace color
}  // namespace isyntax
