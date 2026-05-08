/*
  BSD 2-Clause License

  Copyright (c) 2019-2025, Pieter Valkema

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include "fastslide/readers/isyntax/third_party/isyntax_types.h"

typedef struct block_allocator_t block_allocator_t;
typedef struct isyntax_image_t isyntax_image_t;
typedef struct isyntax_level_t isyntax_level_t;
typedef struct isyntax_t isyntax_t;

namespace aifocore {
class Status;
template <typename T>
class Result;
}  // namespace aifocore

namespace isyntax {
namespace tile {

uint32_t GetAdjacentTilesMask(const isyntax_level_t* level, int32_t tile_x,
                              int32_t tile_y);
uint32_t GetAdjacentTilesMaskOnlyExisting(const isyntax_level_t* level,
                                          int32_t tile_x, int32_t tile_y);
aifocore::Result<uint32_t> IdwtTileForColorChannel(
    isyntax_t* isyntax, isyntax_image_t* wsi, int32_t scale, int32_t tile_x,
    int32_t tile_y, int32_t color, icoeff_t* dest_buffer);

// Distribute the decoded LL region from an IDWT buffer into the 4 child tiles
// at scale-1 for a single color channel.
aifocore::Status DistributeLlToChildrenFromIdwt(
    isyntax_image_t* wsi, int32_t scale, int32_t tile_x, int32_t tile_y,
    int32_t color, const icoeff_t* idwt, int32_t idwt_stride,
    int32_t first_valid_pixel, int32_t block_width, int32_t block_height,
    block_allocator_t* ll_coeff_block_allocator);

// Load a tile (decode coefficients -> IDWT -> optional YCoCg->RGB conversion).
aifocore::Status LoadTile(isyntax_t* isyntax, isyntax_image_t* wsi,
                          int32_t scale, int32_t tile_x, int32_t tile_y,
                          block_allocator_t* ll_coeff_block_allocator,
                          uint32_t* out_buffer_or_null,
                          enum isyntax_pixel_format_t pixel_format);

}  // namespace tile
}  // namespace isyntax
