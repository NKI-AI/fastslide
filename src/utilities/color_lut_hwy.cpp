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

#include "fastslide/utilities/color_lut_hwy.h"

#include <cstddef>
#include <cstdint>

#undef HWY_TARGET_INCLUDE
#if defined(__has_include)
#if __has_include("aifo/fastslide/src/utilities/color_lut_hwy.cpp")
#define HWY_TARGET_INCLUDE "aifo/fastslide/src/utilities/color_lut_hwy.cpp"
#elif __has_include("src/utilities/color_lut_hwy.cpp")
#define HWY_TARGET_INCLUDE "src/utilities/color_lut_hwy.cpp"
#else
#define HWY_TARGET_INCLUDE "color_lut_hwy.cpp"
#endif
#else
#define HWY_TARGET_INCLUDE "color_lut_hwy.cpp"
#endif
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();

namespace fastslide {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// RGBA: pixels are 4 interleaved bytes and can be treated as a single u32
// (little-endian R | G<<8 | B<<16 | A<<24). The LUT index is the low 24 bits;
// the gathered word supplies the transformed R/G/B and the original alpha is
// OR'd back in.
void ApplyRgbaLut(const uint8_t* HWY_RESTRICT lut, uint8_t* HWY_RESTRICT data,
                  size_t pixel_count) {
  const hn::ScalableTag<uint32_t> d32;
  const hn::RebindToSigned<decltype(d32)> di32;
  const size_t N = hn::Lanes(d32);

  const auto rgb_mask = hn::Set(d32, 0x00FFFFFFu);
  const auto alpha_mask = hn::Set(d32, 0xFF000000u);
  const auto three = hn::Set(d32, 3u);
  const auto* base = reinterpret_cast<const uint32_t*>(lut);

  size_t i = 0;
  for (; i + N <= pixel_count; i += N) {
    const auto pixel = hn::LoadU(d32, reinterpret_cast<uint32_t*>(data) + i);
    const auto idx = hn::And(pixel, rgb_mask);
    const auto off = hn::BitCast(di32, hn::Mul(idx, three));  // byte offset.
    const auto gathered = hn::GatherOffset(d32, base, off);
    const auto out =
        hn::Or(hn::And(gathered, rgb_mask), hn::And(pixel, alpha_mask));
    hn::StoreU(out, d32, reinterpret_cast<uint32_t*>(data) + i);
  }
  for (; i < pixel_count; ++i) {
    uint8_t* p = data + i * 4;
    const uint32_t idx = static_cast<uint32_t>(p[0]) |
                         (static_cast<uint32_t>(p[1]) << 8) |
                         (static_cast<uint32_t>(p[2]) << 16);
    const uint8_t* e = lut + static_cast<size_t>(idx) * 3;
    p[0] = e[0];
    p[1] = e[1];
    p[2] = e[2];
  }
}

// RGB: pixels are 3 interleaved bytes. Deinterleave to R/G/B lanes, compute
// 32-bit indices per pixel, gather transformed words, then re-interleave.
void ApplyRgbLut(const uint8_t* HWY_RESTRICT lut, uint8_t* HWY_RESTRICT data,
                 size_t pixel_count) {
  const hn::ScalableTag<uint8_t> d8;
  const hn::ScalableTag<uint32_t> d32;
  const hn::RebindToSigned<decltype(d32)> di32;
  const size_t N8 = hn::Lanes(d8);
  const size_t N32 = hn::Lanes(d32);
  const auto* base = reinterpret_cast<const uint32_t*>(lut);

  HWY_ALIGN uint8_t ra[hn::MaxLanes(d8)];
  HWY_ALIGN uint8_t ga[hn::MaxLanes(d8)];
  HWY_ALIGN uint8_t ba[hn::MaxLanes(d8)];
  HWY_ALIGN uint8_t out_r[hn::MaxLanes(d8)];
  HWY_ALIGN uint8_t out_g[hn::MaxLanes(d8)];
  HWY_ALIGN uint8_t out_b[hn::MaxLanes(d8)];

  size_t i = 0;
  for (; i + N8 <= pixel_count; i += N8) {
    uint8_t* p = data + i * 3;
    hn::VFromD<decltype(d8)> vr, vg, vb;
    hn::LoadInterleaved3(d8, p, vr, vg, vb);
    hn::Store(vr, d8, ra);
    hn::Store(vg, d8, ga);
    hn::Store(vb, d8, ba);

    for (size_t j = 0; j < N8; j += N32) {
      HWY_ALIGN int32_t offs[hn::MaxLanes(d32)];
      for (size_t k = 0; k < N32; ++k) {
        const uint32_t idx = static_cast<uint32_t>(ra[j + k]) |
                             (static_cast<uint32_t>(ga[j + k]) << 8) |
                             (static_cast<uint32_t>(ba[j + k]) << 16);
        offs[k] = static_cast<int32_t>(idx * 3u);  // byte offset.
      }
      const auto voff = hn::Load(di32, offs);
      const auto gathered = hn::GatherOffset(d32, base, voff);
      HWY_ALIGN uint32_t gv[hn::MaxLanes(d32)];
      hn::Store(gathered, d32, gv);
      for (size_t k = 0; k < N32; ++k) {
        out_r[j + k] = static_cast<uint8_t>(gv[k]);
        out_g[j + k] = static_cast<uint8_t>(gv[k] >> 8);
        out_b[j + k] = static_cast<uint8_t>(gv[k] >> 16);
      }
    }

    const auto vor = hn::Load(d8, out_r);
    const auto vog = hn::Load(d8, out_g);
    const auto vob = hn::Load(d8, out_b);
    hn::StoreInterleaved3(vor, vog, vob, d8, p);
  }
  for (; i < pixel_count; ++i) {
    uint8_t* p = data + i * 3;
    const uint32_t idx = static_cast<uint32_t>(p[0]) |
                         (static_cast<uint32_t>(p[1]) << 8) |
                         (static_cast<uint32_t>(p[2]) << 16);
    const uint8_t* e = lut + static_cast<size_t>(idx) * 3;
    p[0] = e[0];
    p[1] = e[1];
    p[2] = e[2];
  }
}

void ApplyRgbLutInterleavedImpl(const uint8_t* lut, uint8_t* data,
                                size_t pixel_count, uint32_t channels) {
  if (channels == 4) {
    ApplyRgbaLut(lut, data, pixel_count);
  } else if (channels == 3) {
    ApplyRgbLut(lut, data, pixel_count);
  }
}

}  // namespace HWY_NAMESPACE
}  // namespace fastslide

HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fastslide {

HWY_EXPORT(ApplyRgbLutInterleavedImpl);

void ApplyRgbLutInterleavedHwy(const uint8_t* lut, uint8_t* data,
                               size_t pixel_count, uint32_t channels) {
  HWY_DYNAMIC_DISPATCH(ApplyRgbLutInterleavedImpl)
  (lut, data, pixel_count, channels);
}

}  // namespace fastslide
#endif  // HWY_ONCE
