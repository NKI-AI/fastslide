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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_RESAMPLE_MKS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_RESAMPLE_MKS_H_

#include "fastslide/runtime/tile_writer/fs_profile.h"

namespace fastslide::runtime {

/// @brief Resample tile with subpixel shift using separable MKS convolution
void ResampleTileSubpixel(const float* src_linear_planar, int w, int h,
                          double frac_x, double frac_y,
                          float* dst_linear_planar);

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_RESAMPLE_MKS_H_
