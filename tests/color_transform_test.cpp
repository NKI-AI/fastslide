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

#include "fastslide/utilities/color_transform.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <lcms2.h>

#include "fastslide/image.h"
#include "fastslide/slide_options.h"

namespace {

// Serialize a freshly built sRGB profile so we can feed it to IccTransform as
// if it were an embedded slide profile.
std::vector<uint8_t> MakeSRGBProfileBytes() {
  cmsHPROFILE profile = cmsCreate_sRGBProfile();
  EXPECT_NE(profile, nullptr);
  cmsUInt32Number size = 0;
  EXPECT_NE(cmsSaveProfileToMem(profile, nullptr, &size), 0);
  std::vector<uint8_t> bytes(size);
  EXPECT_NE(cmsSaveProfileToMem(profile, bytes.data(), &size), 0);
  cmsCloseProfile(profile);
  bytes.resize(size);
  return bytes;
}

// Serialize an RGB profile with the sRGB primaries but *distinct* per-channel
// tone curves (gamma 1.8 / 2.2 / 2.6). Because R, G and B are mapped
// differently, any channel mix-up between the fast and slow paths (e.g. an
// R<->B swap in the index or gather) changes the output and is caught by the
// exhaustive parity tests below.
std::vector<uint8_t> MakeAsymmetricProfileBytes() {
  cmsCIExyY white_point;
  cmsWhitePointFromTemp(&white_point, 6504.0);
  const cmsCIExyYTRIPLE primaries = {
      {0.6400, 0.3300, 1.0},
      {0.3000, 0.6000, 1.0},
      {0.1500, 0.0600, 1.0},
  };
  cmsToneCurve* r_curve = cmsBuildGamma(nullptr, 1.8);
  cmsToneCurve* g_curve = cmsBuildGamma(nullptr, 2.2);
  cmsToneCurve* b_curve = cmsBuildGamma(nullptr, 2.6);
  cmsToneCurve* curves[3] = {r_curve, g_curve, b_curve};
  cmsHPROFILE profile = cmsCreateRGBProfile(&white_point, &primaries, curves);
  cmsFreeToneCurve(r_curve);
  cmsFreeToneCurve(g_curve);
  cmsFreeToneCurve(b_curve);
  EXPECT_NE(profile, nullptr);
  cmsUInt32Number size = 0;
  EXPECT_NE(cmsSaveProfileToMem(profile, nullptr, &size), 0);
  std::vector<uint8_t> bytes(size);
  EXPECT_NE(cmsSaveProfileToMem(profile, bytes.data(), &size), 0);
  cmsCloseProfile(profile);
  bytes.resize(size);
  return bytes;
}

fastslide::Image MakeRgbGradient() {
  fastslide::Image image({4, 4}, fastslide::ImageFormat::kRGB,
                         fastslide::DataType::kUInt8);
  auto* data = image.GetData();
  const size_t pixels = image.GetPixelCount();
  for (size_t i = 0; i < pixels; ++i) {
    data[i * 3 + 0] = static_cast<uint8_t>(i * 7);
    data[i * 3 + 1] = static_cast<uint8_t>(i * 11 + 3);
    data[i * 3 + 2] = static_cast<uint8_t>(255 - i * 5);
  }
  return image;
}

fastslide::Image MakeRgbaGradient() {
  fastslide::Image image({4, 4}, fastslide::ImageFormat::kRGBA,
                         fastslide::DataType::kUInt8);
  auto* data = image.GetData();
  const size_t pixels = image.GetPixelCount();
  for (size_t i = 0; i < pixels; ++i) {
    data[i * 4 + 0] = static_cast<uint8_t>(i * 7);
    data[i * 4 + 1] = static_cast<uint8_t>(i * 11 + 3);
    data[i * 4 + 2] = static_cast<uint8_t>(255 - i * 5);
    data[i * 4 + 3] = static_cast<uint8_t>(i * 13 + 1);  // Alpha.
  }
  return image;
}

fastslide::Image MakeRgb16Gradient() {
  fastslide::Image image({4, 4}, fastslide::ImageFormat::kRGB,
                         fastslide::DataType::kUInt16);
  auto* data = image.GetDataAs<uint16_t>();
  const size_t pixels = image.GetPixelCount();
  for (size_t i = 0; i < pixels; ++i) {
    data[i * 3 + 0] = static_cast<uint16_t>(i * 700);
    data[i * 3 + 1] = static_cast<uint16_t>(i * 1100 + 3);
    data[i * 3 + 2] = static_cast<uint16_t>(65535 - i * 500);
  }
  return image;
}

TEST(IccTransformTest, CreateFromValidProfileSucceeds) {
  const auto bytes = MakeSRGBProfileBytes();
  auto transform_or =
      fastslide::IccTransform::Create(bytes, fastslide::ColorSpace::kSRGB,
                                      fastslide::RenderingIntent::kPerceptual);
  ASSERT_TRUE(transform_or.ok()) << transform_or.status().ToString();
  EXPECT_NE(transform_or.value(), nullptr);
}

TEST(IccTransformTest, CreateFromGarbageBytesFails) {
  const std::vector<uint8_t> garbage(64, 0xAB);
  auto transform_or =
      fastslide::IccTransform::Create(garbage, fastslide::ColorSpace::kSRGB,
                                      fastslide::RenderingIntent::kPerceptual);
  EXPECT_FALSE(transform_or.ok());
}

TEST(IccTransformTest, SRGBToSRGBIsNearIdentity) {
  const auto bytes = MakeSRGBProfileBytes();
  auto transform_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric);
  ASSERT_TRUE(transform_or.ok()) << transform_or.status().ToString();
  const auto transform = std::move(transform_or.value());

  fastslide::Image image = MakeRgbGradient();
  const fastslide::Image original = image;

  const auto status = transform->ApplyInPlace(image);
  ASSERT_TRUE(status.ok()) << status.ToString();

  // sRGB -> sRGB should round-trip within a small quantization tolerance.
  const auto* out = image.GetData();
  const auto* in = original.GetData();
  const size_t n = image.GetPixelCount() * 3;
  for (size_t i = 0; i < n; ++i) {
    EXPECT_NEAR(static_cast<int>(out[i]), static_cast<int>(in[i]), 2)
        << "channel byte " << i;
  }
}

TEST(IccTransformTest, ApplyInPlaceOnEmptyImageIsNoOp) {
  const auto bytes = MakeSRGBProfileBytes();
  auto transform_or =
      fastslide::IccTransform::Create(bytes, fastslide::ColorSpace::kSRGB,
                                      fastslide::RenderingIntent::kPerceptual);
  ASSERT_TRUE(transform_or.ok());
  const auto transform = std::move(transform_or.value());

  fastslide::Image empty;
  EXPECT_TRUE(transform->ApplyInPlace(empty).ok());
}

TEST(IccTransformTest, ApplyInPlaceOnEmptyImageIsNoOpWithLut) {
  const auto bytes = MakeSRGBProfileBytes();
  auto transform_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kPerceptual, /*build_lut=*/true);
  ASSERT_TRUE(transform_or.ok()) << transform_or.status().ToString();
  const auto transform = std::move(transform_or.value());

  fastslide::Image empty;
  EXPECT_TRUE(transform->ApplyInPlace(empty).ok());
}

// The LUT is materialized from the same lcms2 8-bit transform used by the
// non-LUT path, so its output must be byte-identical for 8-bit RGB.
TEST(IccTransformTest, LutMatchesLcmsForRgb) {
  const auto bytes = MakeSRGBProfileBytes();

  auto lcms_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric);
  ASSERT_TRUE(lcms_or.ok()) << lcms_or.status().ToString();
  auto lut_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric, /*build_lut=*/true);
  ASSERT_TRUE(lut_or.ok()) << lut_or.status().ToString();

  fastslide::Image lcms_image = MakeRgbGradient();
  fastslide::Image lut_image = MakeRgbGradient();
  ASSERT_TRUE(lcms_or.value()->ApplyInPlace(lcms_image).ok());
  ASSERT_TRUE(lut_or.value()->ApplyInPlace(lut_image).ok());

  EXPECT_EQ(lcms_image.GetDataVector(), lut_image.GetDataVector());
}

// RGBA parity: color channels match lcms2 and alpha is preserved verbatim.
TEST(IccTransformTest, LutMatchesLcmsForRgbaAndPreservesAlpha) {
  const auto bytes = MakeSRGBProfileBytes();

  auto lcms_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric);
  ASSERT_TRUE(lcms_or.ok()) << lcms_or.status().ToString();
  auto lut_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric, /*build_lut=*/true);
  ASSERT_TRUE(lut_or.ok()) << lut_or.status().ToString();

  const fastslide::Image original = MakeRgbaGradient();
  fastslide::Image lcms_image = MakeRgbaGradient();
  fastslide::Image lut_image = MakeRgbaGradient();
  ASSERT_TRUE(lcms_or.value()->ApplyInPlace(lcms_image).ok());
  ASSERT_TRUE(lut_or.value()->ApplyInPlace(lut_image).ok());

  EXPECT_EQ(lcms_image.GetDataVector(), lut_image.GetDataVector());

  // Alpha bytes untouched by the LUT path.
  const auto* out = lut_image.GetData();
  const auto* in = original.GetData();
  const size_t pixels = lut_image.GetPixelCount();
  for (size_t i = 0; i < pixels; ++i) {
    EXPECT_EQ(out[i * 4 + 3], in[i * 4 + 3]) << "alpha pixel " << i;
  }
}

// A LUT-enabled transform still color-manages 16-bit images by falling back to
// the lcms2 path (the LUT only covers 8-bit).
TEST(IccTransformTest, LutRequestedFallsBackFor16Bit) {
  const auto bytes = MakeSRGBProfileBytes();

  auto lcms_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric);
  ASSERT_TRUE(lcms_or.ok()) << lcms_or.status().ToString();
  auto lut_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric, /*build_lut=*/true);
  ASSERT_TRUE(lut_or.ok()) << lut_or.status().ToString();

  fastslide::Image lcms_image = MakeRgb16Gradient();
  fastslide::Image lut_image = MakeRgb16Gradient();
  ASSERT_TRUE(lcms_or.value()->ApplyInPlace(lcms_image).ok());
  ASSERT_TRUE(lut_or.value()->ApplyInPlace(lut_image).ok());

  // Both took the same lcms2 16-bit path, so results agree exactly.
  EXPECT_EQ(lcms_image.GetDataVector(), lut_image.GetDataVector());
}

// Exhaustive pixel-to-pixel parity: transform every one of the 256^3 RGB
// triples through the slow path (lcms2 `ApplyInPlace`) and the fast path
// (Highway LUT gather) and assert byte-identical output for all ~16.7M pixels.
// Uses an asymmetric profile so a channel mix-up cannot slip through.
TEST(IccTransformTest, FastPathMatchesSlowPathForAllRgbTriples) {
  const auto bytes = MakeAsymmetricProfileBytes();

  auto slow_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric, /*build_lut=*/false);
  ASSERT_TRUE(slow_or.ok()) << slow_or.status().ToString();
  auto fast_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric, /*build_lut=*/true);
  ASSERT_TRUE(fast_or.ok()) << fast_or.status().ToString();

  // 4096 * 4096 == 256^3 pixels, so every RGB triple appears exactly once.
  constexpr uint32_t kSide = 4096;
  fastslide::Image slow_image({kSide, kSide}, fastslide::ImageFormat::kRGB,
                              fastslide::DataType::kUInt8);
  auto* data = slow_image.GetData();
  const size_t pixels = slow_image.GetPixelCount();
  for (size_t i = 0; i < pixels; ++i) {
    data[i * 3 + 0] = static_cast<uint8_t>(i & 0xFF);          // R
    data[i * 3 + 1] = static_cast<uint8_t>((i >> 8) & 0xFF);   // G
    data[i * 3 + 2] = static_cast<uint8_t>((i >> 16) & 0xFF);  // B
  }
  fastslide::Image fast_image = slow_image;

  ASSERT_TRUE(slow_or.value()->ApplyInPlace(slow_image).ok());
  ASSERT_TRUE(fast_or.value()->ApplyInPlace(fast_image).ok());

  ASSERT_EQ(slow_image.SizeBytes(), fast_image.SizeBytes());
  const auto* slow = slow_image.GetData();
  const auto* fast = fast_image.GetData();
  size_t first_mismatch = pixels;
  for (size_t i = 0; i < pixels; ++i) {
    if (slow[i * 3 + 0] != fast[i * 3 + 0] ||
        slow[i * 3 + 1] != fast[i * 3 + 1] ||
        slow[i * 3 + 2] != fast[i * 3 + 2]) {
      first_mismatch = i;
      break;
    }
  }
  ASSERT_EQ(first_mismatch, pixels)
      << "mismatch at pixel " << first_mismatch
      << " (R=" << (first_mismatch & 0xFF)
      << " G=" << ((first_mismatch >> 8) & 0xFF)
      << " B=" << ((first_mismatch >> 16) & 0xFF) << "): slow=("
      << static_cast<int>(slow[first_mismatch * 3 + 0]) << ","
      << static_cast<int>(slow[first_mismatch * 3 + 1]) << ","
      << static_cast<int>(slow[first_mismatch * 3 + 2]) << ") fast=("
      << static_cast<int>(fast[first_mismatch * 3 + 0]) << ","
      << static_cast<int>(fast[first_mismatch * 3 + 1]) << ","
      << static_cast<int>(fast[first_mismatch * 3 + 2]) << ")";
}

// Same exhaustive check for the RGBA (u32 gather) path: every RGB triple with
// a varying alpha, asserting the color channels match the slow path and alpha
// is preserved verbatim.
TEST(IccTransformTest, FastPathMatchesSlowPathForAllRgbaTriples) {
  const auto bytes = MakeAsymmetricProfileBytes();

  auto slow_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric, /*build_lut=*/false);
  ASSERT_TRUE(slow_or.ok()) << slow_or.status().ToString();
  auto fast_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kRelativeColorimetric, /*build_lut=*/true);
  ASSERT_TRUE(fast_or.ok()) << fast_or.status().ToString();

  constexpr uint32_t kSide = 4096;
  fastslide::Image slow_image({kSide, kSide}, fastslide::ImageFormat::kRGBA,
                              fastslide::DataType::kUInt8);
  auto* data = slow_image.GetData();
  const size_t pixels = slow_image.GetPixelCount();
  for (size_t i = 0; i < pixels; ++i) {
    data[i * 4 + 0] = static_cast<uint8_t>(i & 0xFF);              // R
    data[i * 4 + 1] = static_cast<uint8_t>((i >> 8) & 0xFF);       // G
    data[i * 4 + 2] = static_cast<uint8_t>((i >> 16) & 0xFF);      // B
    data[i * 4 + 3] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);  // Alpha.
  }
  const fastslide::Image original = slow_image;
  fastslide::Image fast_image = slow_image;

  ASSERT_TRUE(slow_or.value()->ApplyInPlace(slow_image).ok());
  ASSERT_TRUE(fast_or.value()->ApplyInPlace(fast_image).ok());

  const auto* slow = slow_image.GetData();
  const auto* fast = fast_image.GetData();
  const auto* orig = original.GetData();
  size_t first_mismatch = pixels;
  for (size_t i = 0; i < pixels; ++i) {
    const bool rgb_ok = slow[i * 4 + 0] == fast[i * 4 + 0] &&
                        slow[i * 4 + 1] == fast[i * 4 + 1] &&
                        slow[i * 4 + 2] == fast[i * 4 + 2];
    const bool alpha_ok = fast[i * 4 + 3] == orig[i * 4 + 3];
    if (!rgb_ok || !alpha_ok) {
      first_mismatch = i;
      break;
    }
  }
  ASSERT_EQ(first_mismatch, pixels) << "mismatch at pixel " << first_mismatch;
}

}  // namespace
