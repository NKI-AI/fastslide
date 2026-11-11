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

#include "fastslide/runtime/decoders/j2k_decoder.h"

#include <gtest/gtest.h>
#include <openjpeg.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace fastslide::runtime::decoders {
namespace {

struct ImageDeleter {
  void operator()(opj_image_t* image) const { opj_image_destroy(image); }
};

using ImagePtr = std::unique_ptr<opj_image_t, ImageDeleter>;

struct ComponentSpec {
  uint32_t sub_x = 1;
  uint32_t sub_y = 1;
  uint32_t prec = 8;
  bool sgnd = false;
};

// Three components of a w x h image filled with random in-range samples, plus
// the extremes of the range so clamping is exercised.
ImagePtr MakeImage(uint32_t w, uint32_t h,
                   const std::array<ComponentSpec, 3>& specs, uint32_t seed) {
  std::array<opj_image_cmptparm_t, 3> params{};
  for (int c = 0; c < 3; ++c) {
    params[c].dx = specs[c].sub_x;
    params[c].dy = specs[c].sub_y;
    params[c].w = w / specs[c].sub_x;
    params[c].h = h / specs[c].sub_y;
    params[c].prec = specs[c].prec;
    params[c].sgnd = specs[c].sgnd ? 1 : 0;
  }
  ImagePtr image(opj_image_create(3, params.data(), OPJ_CLRSPC_SRGB));
  if (!image) {
    return image;
  }
  std::mt19937 rng(seed);
  for (int c = 0; c < 3; ++c) {
    const auto& spec = specs[c];
    const int64_t lo = spec.sgnd ? -(int64_t{1} << (spec.prec - 1)) : 0;
    const int64_t hi = spec.sgnd ? (int64_t{1} << (spec.prec - 1)) - 1
                                 : (int64_t{1} << spec.prec) - 1;
    std::uniform_int_distribution<int64_t> dist(lo, hi);
    auto& comp = image->comps[c];
    const size_t n = static_cast<size_t>(comp.w) * comp.h;
    for (size_t i = 0; i < n; ++i) {
      comp.data[i] = static_cast<OPJ_INT32>(dist(rng));
    }
    comp.data[0] = static_cast<OPJ_INT32>(lo);
    comp.data[n - 1] = static_cast<OPJ_INT32>(hi);
  }
  return image;
}

// The conversion as it was before the per-component parameters were hoisted
// out of the pixel loop; the optimised version must match it byte for byte.
std::vector<uint8_t> ReferenceConvert(const opj_image_t& image, uint32_t w,
                                      uint32_t h, J2kColorspace colorspace) {
  std::array<uint32_t, 3> sub_x{};
  std::array<uint32_t, 3> sub_y{};
  for (int c = 0; c < 3; ++c) {
    sub_x[c] = w / image.comps[c].w;
    sub_y[c] = h / image.comps[c].h;
  }
  auto normalize = [](OPJ_INT32 val, const opj_image_comp_t& comp) -> int32_t {
    if (comp.sgnd) {
      val += (1 << (comp.prec - 1));
    }
    if (comp.prec > 8) {
      val >>= (comp.prec - 8);
    } else if (comp.prec < 8) {
      val <<= (8 - comp.prec);
    }
    return std::clamp(val, OPJ_INT32{0}, OPJ_INT32{255});
  };
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
  for (uint32_t y = 0; y < h; ++y) {
    const OPJ_INT32* r0 = image.comps[0].data +
                          static_cast<size_t>(y / sub_y[0]) * image.comps[0].w;
    const OPJ_INT32* r1 = image.comps[1].data +
                          static_cast<size_t>(y / sub_y[1]) * image.comps[1].w;
    const OPJ_INT32* r2 = image.comps[2].data +
                          static_cast<size_t>(y / sub_y[2]) * image.comps[2].w;
    uint8_t* dst = rgb.data() + static_cast<size_t>(y) * w * 3;
    for (uint32_t x = 0; x < w; ++x) {
      const int32_t a = normalize(r0[x / sub_x[0]], image.comps[0]);
      const int32_t b = normalize(r1[x / sub_x[1]], image.comps[1]);
      const int32_t c = normalize(r2[x / sub_x[2]], image.comps[2]);
      if (colorspace == J2kColorspace::kYCbCr) {
        const int32_t cb = b - 128;
        const int32_t cr = c - 128;
        const int32_t r = a + ((91881 * cr + 32768) >> 16);
        const int32_t g = a - ((22554 * cb + 46802 * cr + 32768) >> 16);
        const int32_t bl = a + ((116130 * cb + 32768) >> 16);
        dst[x * 3 + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
        dst[x * 3 + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
        dst[x * 3 + 2] = static_cast<uint8_t>(std::clamp(bl, 0, 255));
      } else {
        dst[x * 3 + 0] = static_cast<uint8_t>(a);
        dst[x * 3 + 1] = static_cast<uint8_t>(b);
        dst[x * 3 + 2] = static_cast<uint8_t>(c);
      }
    }
  }
  return rgb;
}

TEST(J2kConvertTest, MatchesReferenceAcrossPrecisionSignAndSubsampling) {
  constexpr uint32_t kW = 38;
  constexpr uint32_t kH = 30;
  uint32_t seed = 1;
  for (const uint32_t prec : {8u, 7u, 12u, 16u}) {
    for (const bool sgnd : {false, true}) {
      for (const auto [chroma_x, chroma_y] :
           {std::pair{1u, 1u}, std::pair{2u, 1u}, std::pair{2u, 2u}}) {
        for (const auto colorspace :
             {J2kColorspace::kRgb, J2kColorspace::kYCbCr}) {
          const std::array<ComponentSpec, 3> specs{{
              {1, 1, prec, sgnd},
              {chroma_x, chroma_y, prec, sgnd},
              {chroma_x, chroma_y, prec, sgnd},
          }};
          auto image = MakeImage(kW, kH, specs, seed++);
          ASSERT_NE(image, nullptr);
          auto converted = detail::ConvertJ2kToRgb8(*image, kW, kH, colorspace);
          ASSERT_TRUE(converted.ok()) << converted.status().ToString();
          EXPECT_EQ(converted->width, kW);
          EXPECT_EQ(converted->height, kH);
          EXPECT_EQ(converted->rgb,
                    ReferenceConvert(*image, kW, kH, colorspace))
              << "prec=" << prec << " sgnd=" << sgnd << " chroma=" << chroma_x
              << "x" << chroma_y
              << " ycbcr=" << (colorspace == J2kColorspace::kYCbCr);
        }
      }
    }
  }
}

TEST(J2kConvertTest, RejectsNonIntegerSubsampling) {
  const std::array<ComponentSpec, 3> specs{{{1, 1}, {2, 2}, {2, 2}}};
  auto image = MakeImage(38, 30, specs, 7);
  ASSERT_NE(image, nullptr);
  // 37 columns cannot be split into 19 chroma columns.
  auto converted =
      detail::ConvertJ2kToRgb8(*image, 37, 30, J2kColorspace::kYCbCr);
  EXPECT_FALSE(converted.ok());
}

}  // namespace
}  // namespace fastslide::runtime::decoders
