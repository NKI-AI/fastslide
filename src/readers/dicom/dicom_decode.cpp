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

/// @file dicom_decode.cpp
/// @brief Implementation of the shared DICOM frame decode helper.

#include "fastslide/readers/dicom/dicom_decode.h"

#include <utility>

#include "aifocore/status/result.h"
#include "fastslide/runtime/decoders/j2k_decoder.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"

namespace fastslide {
namespace dicom {
namespace internal {

aifocore::Result<std::vector<uint8_t>> DecodeDicomFrameBytes(
    std::span<const uint8_t> frame_bytes, DicomTransferSyntax syntax,
    DicomPhotometric photometric, uint32_t expected_width,
    uint32_t expected_height) {
  switch (syntax) {
    case DicomTransferSyntax::kExplicitVRLittleEndian: {
      const size_t expected =
          static_cast<size_t>(expected_width) * expected_height * 3;
      if (frame_bytes.size() != expected) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                    "Uncompressed RGB frame size mismatch");
      }
      return std::vector<uint8_t>(frame_bytes.begin(), frame_bytes.end());
    }

    case DicomTransferSyntax::kJpegBaseline: {
      // jpgd auto-converts YCbCr to RGB via JCS_YCbCr; the photometric tag is
      // therefore informational only for this branch.
      AIFOCORE_ASSIGN_OR_RETURN(
          auto decoded, runtime::decoders::DecodeJpegToRgb(frame_bytes));
      return std::move(decoded.rgb);
    }

    case DicomTransferSyntax::kJpeg2000:
    case DicomTransferSyntax::kJpeg2000Lossless: {
      runtime::decoders::J2kDecodeOptions opts;
      opts.colorspace = (photometric == DicomPhotometric::kYbrIct)
                            ? runtime::decoders::J2kColorspace::kYCbCr
                            : runtime::decoders::J2kColorspace::kRgb;
      AIFOCORE_ASSIGN_OR_RETURN(
          auto decoded, runtime::decoders::DecodeJ2kToRgb(frame_bytes, opts));
      return std::move(decoded.rgb);
    }

    case DicomTransferSyntax::kUnsupported:
    default:
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                                  "Unsupported DICOM transfer syntax");
  }
}

}  // namespace internal
}  // namespace dicom
}  // namespace fastslide
