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

#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include <jpeg-compressor/jpgd.h>
#include <cstdlib>
#include <cstring>


#include "aifocore/status/result.h"

namespace fastslide::runtime::decoders {

aifocore::Result<DecodedRgb> DecodeJpegToRgb(
    std::span<const uint8_t> jpeg_bytes, const JpegDecodeOptions& options) {
  if (jpeg_bytes.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "JPEG input is empty");
  }

  int actual_comps = 0;
  int width = 0;
  int height = 0;

  uint32_t flags = 0;
  if (options.no_ycbcr_conversion) {
    flags |= jpgd::jpeg_decoder::cFlagNoYCbCrConversion;
  }

  // Request 3 output components (RGB).
  unsigned char* decoded = jpgd::decompress_jpeg_image_from_memory(
      jpeg_bytes.data(), static_cast<int>(jpeg_bytes.size()), &width, &height,
      &actual_comps, 3, flags);
  if (decoded == nullptr || width <= 0 || height <= 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "jpgd JPEG decompression failed");
  }

  DecodedRgb out{};
  out.width = static_cast<uint32_t>(width);
  out.height = static_cast<uint32_t>(height);
  out.rgb.resize(static_cast<size_t>(width) * height * 3);
  std::memcpy(out.rgb.data(), decoded, out.rgb.size());

  std::free(decoded);
  return out;
}

}  // namespace fastslide::runtime::decoders
