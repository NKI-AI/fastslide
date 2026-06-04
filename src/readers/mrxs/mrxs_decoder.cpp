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

#include "fastslide/readers/mrxs/mrxs_decoder.h"

#include <cstring>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/runtime/decoders/bmp_decoder.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/runtime/decoders/jpeg_xr_decoder.h"
#include "fastslide/runtime/decoders/png_decoder.h"

namespace fastslide::mrxs::internal {
namespace {

/// @brief Wrap a `runtime::decoders::DecodedRgb` into an `RGBImage`.
///
/// The `RGBImage` owns its own contiguous buffer, so we copy the decoded
/// pixels into it rather than transferring ownership.
RGBImage RgbToImage(const runtime::decoders::DecodedRgb& decoded) {
  RGBImage img(ImageDimensions{decoded.width, decoded.height},
               ImageFormat::kRGB, DataType::kUInt8);
  std::memcpy(img.GetData(), decoded.rgb.data(), decoded.rgb.size());
  return img;
}

}  // namespace

aifocore::Result<RGBImage> DecodeImage(const std::vector<uint8_t>& data,
                                       MrxsImageFormat format) {
  const std::span<const uint8_t> bytes(data);
  switch (format) {
    case MrxsImageFormat::kJpeg: {
      runtime::decoders::JpegDecodeOptions opts{};
      opts.no_ycbcr_conversion = true;
      AIFOCORE_ASSIGN_OR_RETURN(
          auto decoded, runtime::decoders::DecodeJpegToRgb(bytes, opts));
      return RgbToImage(decoded);
    }
    case MrxsImageFormat::kPng: {
      AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                                runtime::decoders::DecodePngToRgb(bytes));
      return RgbToImage(decoded);
    }
    case MrxsImageFormat::kBmp: {
      AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                                runtime::decoders::DecodeBmpToRgb(bytes));
      return RgbToImage(decoded);
    }
    case MrxsImageFormat::kJpegXr: {
      AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                                runtime::decoders::DecodeJpegXrToRgb(bytes));
      return RgbToImage(decoded);
    }
    default:
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Unknown or unsupported image format");
  }
}

aifocore::Result<Image> DecodeImage16(const std::vector<uint8_t>& data,
                                      MrxsImageFormat format) {
  const std::span<const uint8_t> bytes(data);

  runtime::decoders::DecodedRgb16 decoded;
  switch (format) {
    case MrxsImageFormat::kPng: {
      AIFOCORE_ASSIGN_OR_RETURN(decoded,
                                runtime::decoders::DecodePng16ToRgb(bytes));
      break;
    }
    case MrxsImageFormat::kJpegXr: {
      AIFOCORE_ASSIGN_OR_RETURN(decoded,
                                runtime::decoders::DecodeJpegXrToRgb16(bytes));
      break;
    }
    case MrxsImageFormat::kJpeg:
    case MrxsImageFormat::kBmp:
    case MrxsImageFormat::kUnknown:
    default: {
      const char* fmt_name = "Unknown";
      switch (format) {
        case MrxsImageFormat::kJpeg:
          fmt_name = "JPEG";
          break;
        case MrxsImageFormat::kBmp:
          fmt_name = "BMP";
          break;
        case MrxsImageFormat::kPng:
        case MrxsImageFormat::kJpegXr:
        case MrxsImageFormat::kUnknown:
          break;
      }
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format(
              "16-bit MRXS tile decoding is implemented for PNG and JPEG-XR; "
              "got format='{}'. JPEG-2000 16-bit tiles are not supported yet",
              fmt_name));
    }
  }

  Image img(ImageDimensions{decoded.width, decoded.height}, ImageFormat::kRGB,
            DataType::kUInt16);
  std::memcpy(img.GetData(), decoded.rgb.data(),
              decoded.rgb.size() * sizeof(uint16_t));
  return img;
}

}  // namespace fastslide::mrxs::internal
