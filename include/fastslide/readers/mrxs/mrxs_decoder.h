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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DECODER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DECODER_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "fastslide/image.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"

namespace fastslide {
namespace mrxs {
namespace internal {

/// @brief Decode compressed image data into an RGB image
/// @param data Compressed image data
/// @param format Image format (JPEG/PNG/BMP)
/// @return StatusOr containing decoded RGB image or error
absl::StatusOr<RGBImage> DecodeImage(const std::vector<uint8_t>& data,
                                     MrxsImageFormat format);

/// @brief Decode JPEG image data
/// @param data JPEG compressed data
/// @return StatusOr containing decoded RGB image or error
absl::StatusOr<RGBImage> DecodeJpeg(const std::vector<uint8_t>& data);

/// @brief Decode PNG image data (using lodepng)
/// @param data PNG compressed data
/// @return StatusOr containing decoded RGB image or error
absl::StatusOr<RGBImage> DecodePng(const std::vector<uint8_t>& data);

/// @brief Decode BMP image data (simple, uncompressed only)
/// @param data BMP data
/// @return StatusOr containing decoded RGB image or error
absl::StatusOr<RGBImage> DecodeBmp(const std::vector<uint8_t>& data);

}  // namespace internal
}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DECODER_H_
