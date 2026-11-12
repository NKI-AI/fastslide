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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_JPEG_DECODE_MODE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_JPEG_DECODE_MODE_H_

#include <cstdint>

namespace fastslide {

/// @brief JPEG decoder settings, chosen per reader.
///
/// Set with `SlideReader::SetJpegDecodeMode`. Applies to every JPEG decode of
/// the reader: tiles and associated images, through simpletiff (TIFF-based
/// formats) and FastSlide's own decoder (MRXS, DICOM, CZI, VSI, iSyntax).
enum class JpegDecodeMode : uint8_t {
  /// Fast integer IDCT (`JDCT_IFAST`), no fancy upsampling, no block
  /// smoothing. The default.
  kFast,
  /// libjpeg's defaults (`JDCT_ISLOW` and fancy upsampling), as used by
  /// libtiff, OpenSlide and Pillow: pixel-identical to them, a few percent
  /// slower. Needs libjpeg in both FastSlide and simpletiff; not available on
  /// jpgd builds.
  kLibjpeg,
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_JPEG_DECODE_MODE_H_
