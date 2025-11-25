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

// Image writer utilities for saving decoded TIFF data

#ifndef SIMPLETIFF_IMAGE_WRITER_H_
#define SIMPLETIFF_IMAGE_WRITER_H_

#include <cstdint>
#include <string_view>

namespace simpletiff {

/// Write RGB data to PNG file
///
/// @param filename Output filename
/// @param rgb RGB888 data (width * height * 3 bytes)
/// @param width Image width
/// @param height Image height
/// @param stride Row stride in bytes (typically width * 3)
/// @return true on success, false on failure
bool WritePng(std::string_view filename, const uint8_t* rgb, int width,
              int height, int stride);

/// Write grayscale data to PNG file
///
/// @param filename Output filename
/// @param gray Grayscale data (width * height bytes)
/// @param width Image width
/// @param height Image height
/// @param stride Row stride in bytes (typically width)
/// @return true on success, false on failure
bool WritePngGrayscale(std::string_view filename, const uint8_t* gray,
                       int width, int height, int stride);

/// Write RGB data to PPM file (simple uncompressed format)
///
/// @param filename Output filename
/// @param rgb RGB888 data (width * height * 3 bytes)
/// @param width Image width
/// @param height Image height
/// @param stride Row stride in bytes (typically width * 3)
/// @return true on success, false on failure
bool WritePpm(std::string_view filename, const uint8_t* rgb, int width,
              int height, int stride);

}  // namespace simpletiff

#endif  // SIMPLETIFF_IMAGE_WRITER_H_
