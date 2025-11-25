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
//
// TIFF predictor operations

#ifndef SIMPLETIFF_PREDICTOR_H_
#define SIMPLETIFF_PREDICTOR_H_

#include <cstdint>
#include <vector>

namespace simpletiff {

/// Apply horizontal differencing predictor (TIFF Predictor tag 2)
///
/// Reverses horizontal differencing applied before compression.
/// Each pixel becomes: pixel[i] = pixel[i] + pixel[i-1]
///
/// @param data Data to apply predictor to (modified in-place)
/// @param width Image width
/// @param height Image height
/// @param samples_per_pixel Number of samples per pixel (e.g., 3 for RGB)
/// @param bits_per_sample Bits per sample (8, 16, or 32)
/// @param file_big_endian Whether the TIFF file is big-endian
/// @param planar_configuration 1 = CONTIG (interleaved), 2 = SEPARATE (planar)
void ApplyHorizontalPredictor(std::vector<uint8_t>& data, int width, int height,
                              int samples_per_pixel, int bits_per_sample,
                              bool file_big_endian,
                              int planar_configuration = 1);

}  // namespace simpletiff

#endif  // SIMPLETIFF_PREDICTOR_H_
