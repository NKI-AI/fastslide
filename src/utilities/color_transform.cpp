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

#include "fastslide/utilities/color_transform.h"

#include <lcms2.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/image.h"
#include "fastslide/slide_options.h"
#include "fastslide/utilities/color_lut_hwy.h"

namespace fastslide {
namespace {

/// @brief Map a FastSlide rendering intent to the lcms2 INTENT_* constant.
cmsUInt32Number ToLcmsIntent(RenderingIntent intent) {
  switch (intent) {
    case RenderingIntent::kPerceptual:
      return INTENT_PERCEPTUAL;
    case RenderingIntent::kRelativeColorimetric:
      return INTENT_RELATIVE_COLORIMETRIC;
    case RenderingIntent::kSaturation:
      return INTENT_SATURATION;
    case RenderingIntent::kAbsoluteColorimetric:
      return INTENT_ABSOLUTE_COLORIMETRIC;
  }
  return INTENT_PERCEPTUAL;
}

/// @brief Build the lcms2 pixel-format descriptor for an image format/dtype.
///
/// Only interleaved 8-/16-bit RGB and RGBA are supported. Returns 0 for any
/// unsupported combination, signalling that the image should be left untouched.
cmsUInt32Number ToLcmsPixelFormat(ImageFormat format, DataType dtype) {
  const bool is_u8 = dtype == DataType::kUInt8;
  const bool is_u16 = dtype == DataType::kUInt16;
  if (!is_u8 && !is_u16) {
    return 0;
  }
  switch (format) {
    case ImageFormat::kRGB:
      return is_u8 ? TYPE_RGB_8 : TYPE_RGB_16;
    case ImageFormat::kRGBA:
      return is_u8 ? TYPE_RGBA_8 : TYPE_RGBA_16;
    default:
      return 0;
  }
}

/// @brief Create the destination profile for a target color space.
///
/// sRGB (and the RGB/automatic aliases) use the built-in sRGB profile. Linear
/// uses an RGB profile with the sRGB primaries/white point but a linear (gamma
/// 1.0) tone response curve.
cmsHPROFILE CreateTargetProfile(ColorSpace target) {
  if (target != ColorSpace::kLinear) {
    return cmsCreate_sRGBProfile();
  }

  // Linear-light RGB using the sRGB primaries and D65 white point.
  cmsCIExyY white_point;
  cmsWhitePointFromTemp(&white_point, 6504.0);
  const cmsCIExyYTRIPLE primaries = {
      {0.6400, 0.3300, 1.0},
      {0.3000, 0.6000, 1.0},
      {0.1500, 0.0600, 1.0},
  };
  cmsToneCurve* linear_curve = cmsBuildGamma(nullptr, 1.0);
  if (linear_curve == nullptr) {
    return nullptr;
  }
  cmsToneCurve* curves[3] = {linear_curve, linear_curve, linear_curve};
  cmsHPROFILE profile = cmsCreateRGBProfile(&white_point, &primaries, curves);
  cmsFreeToneCurve(linear_curve);
  return profile;
}

}  // namespace

void IccTransform::ProfileDeleter::operator()(void* handle) const noexcept {
  if (handle != nullptr) {
    cmsCloseProfile(handle);
  }
}

void IccTransform::TransformDeleter::operator()(void* handle) const noexcept {
  if (handle != nullptr) {
    cmsDeleteTransform(handle);
  }
}

IccTransform::IccTransform(ProfileHandle src_profile, ProfileHandle dst_profile,
                           RenderingIntent intent)
    : src_profile_(std::move(src_profile)),
      dst_profile_(std::move(dst_profile)),
      intent_(intent) {}

IccTransform::~IccTransform() = default;

aifocore::Result<std::unique_ptr<IccTransform>> IccTransform::Create(
    std::span<const uint8_t> profile_bytes, ColorSpace target,
    RenderingIntent intent, bool build_lut) {
  if (profile_bytes.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "ICC profile is empty");
  }

  ProfileHandle src_profile(cmsOpenProfileFromMem(
      profile_bytes.data(),
      static_cast<cmsUInt32Number>(profile_bytes.size())));
  if (src_profile == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "failed to parse embedded ICC profile");
  }

  ProfileHandle dst_profile(CreateTargetProfile(target));
  if (dst_profile == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "failed to create target color profile");
  }

  // Private constructor; wrap manually since make_unique cannot access it.
  std::unique_ptr<IccTransform> transform(
      new IccTransform(std::move(src_profile), std::move(dst_profile), intent));
  if (build_lut) {
    transform->BuildLut8();
  }
  return transform;
}

void IccTransform::BuildLut8() {
  // Reuse the same lcms2 transform that ApplyInPlace would use for 8-bit RGB,
  // so the LUT is byte-identical to the non-LUT path.
  auto handle_or = GetOrBuildTransform(ImageFormat::kRGB, DataType::kUInt8);
  if (!handle_or.ok() || handle_or.value() == nullptr) {
    return;  // Silent fallback: leave lut8_ empty, ApplyInPlace uses lcms2.
  }
  void* handle = handle_or.value();

  constexpr size_t kEntries = 256UL * 256UL * 256UL;  // All RGB triples.
  // One trailing pad byte: the SIMD gather reads a full 32-bit word at byte
  // offset `idx * 3`, so the final entry's read would otherwise run one byte
  // past the end.
  std::vector<uint8_t> lut(kEntries * 3 + 1);

  // The LUT is indexed by `idx = R | (G << 8) | (B << 16)` (the little-endian
  // value of an interleaved RGB(A) pixel), so red varies fastest along
  // contiguous entries. Fill one (g, b) run of 256 red values at a time and
  // let lcms2 transform it straight into the LUT, keeping the call count
  // modest (~65k `cmsDoTransform` calls).
  std::array<uint8_t, 256 * 3> row{};
  for (int b = 0; b < 256; ++b) {
    for (int g = 0; g < 256; ++g) {
      for (int r = 0; r < 256; ++r) {
        row[r * 3 + 0] = static_cast<uint8_t>(r);
        row[r * 3 + 1] = static_cast<uint8_t>(g);
        row[r * 3 + 2] = static_cast<uint8_t>(b);
      }
      const size_t base_index =
          static_cast<size_t>(g) * 256 + static_cast<size_t>(b) * 65536;
      cmsDoTransform(handle, row.data(), lut.data() + base_index * 3, 256);
    }
  }

  lut8_ = std::move(lut);
}

void IccTransform::ApplyLut8(Image& image) const {
  // Highway-accelerated gather (with a scalar fallback target); alpha (RGBA)
  // is preserved.
  ApplyRgbLutInterleavedHwy(lut8_.data(), image.GetData(),
                            image.GetPixelCount(), image.GetChannels());
}

aifocore::Result<void*> IccTransform::GetOrBuildTransform(
    ImageFormat format, DataType dtype) const {
  const cmsUInt32Number pixel_format = ToLcmsPixelFormat(format, dtype);
  if (pixel_format == 0) {
    return static_cast<void*>(nullptr);  // Unsupported: skip.
  }

  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& entry : cache_) {
    if (entry.format == format && entry.dtype == dtype) {
      return entry.handle.get();
    }
  }

  cmsUInt32Number flags = 0;
  if (format == ImageFormat::kRGBA) {
    flags |= cmsFLAGS_COPY_ALPHA;
  }

  cmsHTRANSFORM handle =
      cmsCreateTransform(src_profile_.get(), pixel_format, dst_profile_.get(),
                         pixel_format, ToLcmsIntent(intent_), flags);
  if (handle == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "failed to create lcms2 color transform for image format");
  }

  cache_.push_back(CachedTransform{format, dtype, TransformHandle(handle)});
  return handle;
}

aifocore::Status IccTransform::ApplyInPlace(Image& image) const {
  if (image.Empty()) {
    return aifocore::Status::OkStatus();
  }

  // lcms2 pixel formats used here are interleaved; a planar buffer would be
  // reinterpreted incorrectly, so leave it untouched.
  if (!image.IsInterleaved()) {
    return aifocore::Status::OkStatus();
  }

  // Fast path: precomputed 256^3 LUT for 8-bit interleaved RGB(A). Output is
  // byte-identical to the lcms2 8-bit transform used to build it.
  const ImageFormat format = image.GetFormat();
  if (!lut8_.empty() && image.GetDataType() == DataType::kUInt8 &&
      (format == ImageFormat::kRGB || format == ImageFormat::kRGBA)) {
    ApplyLut8(image);
    return aifocore::Status::OkStatus();
  }

  void* handle = nullptr;
  AIFOCORE_ASSIGN_OR_RETURN(
      handle, GetOrBuildTransform(image.GetFormat(), image.GetDataType()));
  if (handle == nullptr) {
    return aifocore::Status::OkStatus();  // Unsupported format: no-op.
  }

  const cmsUInt32Number pixel_count =
      static_cast<cmsUInt32Number>(image.GetPixelCount());
  // Source == destination: transform the region buffer in place, no copy.
  cmsDoTransform(handle, image.GetData(), image.GetData(), pixel_count);
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide
