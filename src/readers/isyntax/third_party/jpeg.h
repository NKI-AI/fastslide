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

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "readers/isyntax/third_party/isyntax.h"
#include "readers/isyntax/third_party/isyntax_types.h"

namespace isyntax {
namespace jpeg {

// Swizzle R and B channels in-place (RGBA <-> BGRA).
void SwapRedBlueInPlace(std::span<uint32_t> pixels, int width, int height);

struct DecodedImage {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<uint32_t> pixels;
};

// Decode a JPEG byte stream to pixel buffer (RGBA or BGRA depending on fmt).
aifocore::Result<DecodedImage> DecodeJpeg(std::span<const uint8_t> jpeg_bytes,
                                          isyntax_pixel_format_t fmt);

// Read and base64-decode the associated image JPEG bitstream from the iSyntax
// file.
aifocore::Result<std::vector<uint8_t>> ReadAssociatedImageJpegBytes(
    isyntax_t* isyntax, isyntax_image_t* image);

// Read and base64-decode the ICC profile bitstream from the iSyntax file.
aifocore::Result<std::vector<uint8_t>> ReadIccProfileBytes(
    isyntax_t* isyntax, isyntax_image_t* image);

// Convenience: read associated image JPEG bytes and decode to pixels.
aifocore::Result<DecodedImage> ReadAssociatedImagePixels(
    isyntax_t* isyntax, isyntax_image_t* image, isyntax_pixel_format_t fmt);

}  // namespace jpeg
}  // namespace isyntax
