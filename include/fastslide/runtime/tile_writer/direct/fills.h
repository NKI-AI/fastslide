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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_FILLS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_FILLS_H_

#include <cstddef>
#include <cstdint>

#include "fastslide/runtime/tile_writer/fs_profile.h"

namespace fastslide::runtime {

void ZeroInit(uint8_t* buf, size_t nbytes);

void FillRGB8(uint8_t* buf, int w, int h, uint8_t r, uint8_t g, uint8_t b);

void FillRGBA8(uint8_t* buf, int w, int h, uint8_t r, uint8_t g, uint8_t b,
               uint8_t a);

void FillGray8(uint8_t* buf, int w, int h, uint8_t value);

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_DIRECT_FILLS_H_
