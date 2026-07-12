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

}  // namespace
