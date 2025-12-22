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
// This file incrementally ports parts of `isyntax_tile.c` to C++20.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/color.h"
#include "fastslide/readers/isyntax/third_party/dwt.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/platform/intrinsics.h"
#include "fastslide/readers/isyntax/third_party/tile.h"

namespace isyntax {
namespace tile {

inline void GetOffsettedCoeffBlocks(std::array<icoeff_t*, 4>& ll_hl_lh_hh,
                                    int32_t offset,
                                    const isyntax_tile_channel_t& color_channel,
                                    int32_t block_stride,
                                    icoeff_t* black_dummy_coeff,
                                    icoeff_t* white_dummy_coeff) {
  if (color_channel.coeff_ll != nullptr) {
    ll_hl_lh_hh[0] = color_channel.coeff_ll + offset;  // ll
  } else {
    ll_hl_lh_hh[0] = white_dummy_coeff;
  }

  if (color_channel.coeff_h != nullptr) {
    ll_hl_lh_hh[1] = color_channel.coeff_h + offset;                     // hl
    ll_hl_lh_hh[2] = color_channel.coeff_h + block_stride + offset;      // lh
    ll_hl_lh_hh[3] = color_channel.coeff_h + 2 * block_stride + offset;  // hh
  } else {
    ll_hl_lh_hh[1] = black_dummy_coeff;
    ll_hl_lh_hh[2] = black_dummy_coeff;
    ll_hl_lh_hh[3] = black_dummy_coeff;
  }
}

inline bool IsParentTileMissing(const isyntax_image_t& wsi, int32_t scale,
                                int32_t tile_x, int32_t tile_y) {
  if (scale < wsi.max_scale) {
    const isyntax_level_t& parent_level = wsi.levels[scale + 1];
    const isyntax_tile_t* parent_tile =
        parent_level.tiles + (tile_y / 2) * parent_level.width_in_tiles +
        (tile_x / 2);
    return !parent_tile->exists;
  }
  return false;
}

struct TileProcessingContext {
  isyntax_t* isyntax = nullptr;
  const isyntax_image_t* wsi = nullptr;
  isyntax_level_t* level = nullptr;
  int32_t scale = 0;
  int32_t tile_x = 0;
  int32_t tile_y = 0;
  int32_t color = 0;
  icoeff_t* h_dummy_coeff = nullptr;
  icoeff_t* ll_dummy_coeff = nullptr;
  int32_t block_width = 0;
  int32_t block_height = 0;
  int32_t block_stride = 0;
  int32_t dest_stride = 0;
  std::array<icoeff_t*, 4> quadrants{};
  uint32_t adj_tiles_mask = 0;
  uint32_t* invalid_neighbors_ll = nullptr;
  uint32_t* invalid_neighbors_h = nullptr;
};

void ProcessNeighborTile(TileProcessingContext& ctx, int32_t dy, int32_t dx) {
  const int bit_index = (1 - dy) * 3 + (1 - dx);
  const uint32_t mask_bit = static_cast<uint32_t>(1) << bit_index;

  if ((ctx.adj_tiles_mask & mask_bit) == 0) {
    return;
  }

  isyntax_tile_t* source_tile = ctx.level->tiles +
                                (ctx.tile_y + dy) * ctx.level->width_in_tiles +
                                (ctx.tile_x + dx);
  if (!source_tile->exists) {
    return;
  }

  const isyntax_tile_channel_t& color_channel =
      source_tile->color_channels[ctx.color];

  if (color_channel.coeff_ll == nullptr &&
      !IsParentTileMissing(*ctx.wsi, ctx.scale, ctx.tile_x + dx,
                           ctx.tile_y + dy)) {
    *ctx.invalid_neighbors_ll |= mask_bit;
  }
  if (color_channel.coeff_h == nullptr) {
    *ctx.invalid_neighbors_h |= mask_bit;
  }

  const int32_t pad_l = ISYNTAX_IDWT_PAD_L;
  const int32_t pad_r = ISYNTAX_IDWT_PAD_R;

  const int32_t copy_width_pixels =
      (dx == 0) ? ctx.block_width : ((dx == -1) ? pad_l : pad_r);
  const int32_t copy_height_pixels =
      (dy == 0) ? ctx.block_height : ((dy == -1) ? pad_l : pad_r);

  const int32_t src_off_x = (dx == -1) ? (ctx.block_width - pad_r) : 0;
  const int32_t src_off_y = (dy == -1) ? (ctx.block_height - pad_r) : 0;

  const int32_t dst_off_x =
      (dx == -1) ? 0 : ((dx == 0) ? pad_l : (pad_l + ctx.block_width));
  const int32_t dst_off_y =
      (dy == -1) ? 0 : ((dy == 0) ? pad_l : (pad_l + ctx.block_height));

  const int32_t source_offset = src_off_y * ctx.block_width + src_off_x;

  std::array<icoeff_t*, 4> ll_hl_lh_hh{};
  GetOffsettedCoeffBlocks(ll_hl_lh_hh, source_offset, color_channel,
                          ctx.block_stride, ctx.h_dummy_coeff,
                          ctx.ll_dummy_coeff);

  const size_t row_copy_size =
      static_cast<size_t>(copy_width_pixels) * sizeof(icoeff_t);

  for (int i = 0; i < 4; ++i) {
    icoeff_t* source = ll_hl_lh_hh[i];
    icoeff_t* dest = ctx.quadrants[i] + dst_off_y * ctx.dest_stride + dst_off_x;
    for (int32_t y = 0; y < copy_height_pixels; ++y) {
      std::memcpy(dest, source, row_copy_size);
      source += ctx.block_width;
      dest += ctx.dest_stride;
    }
  }
}

aifocore::Result<uint32_t> IdwtTileForColorChannel(
    isyntax_t* isyntax, isyntax_image_t* wsi, int32_t scale, int32_t tile_x,
    int32_t tile_y, int32_t color, icoeff_t* dest_buffer) {
  if (isyntax == nullptr || wsi == nullptr || dest_buffer == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "IdwtTileForColorChannel: null input");
  }
  isyntax_level_t* level = wsi->levels + scale;
  if (level == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "IdwtTileForColorChannel: null level");
  }
  ASSERT(tile_x >= 0 && tile_x < level->width_in_tiles);
  ASSERT(tile_y >= 0 && tile_y < level->height_in_tiles);

  const uint32_t adj_tiles =
      ::isyntax::tile::GetAdjacentTilesMask(level, tile_x, tile_y);

  const int32_t pad_l = ISYNTAX_IDWT_PAD_L;
  const int32_t pad_r = ISYNTAX_IDWT_PAD_R;
  const int32_t pad_l_plus_r = pad_l + pad_r;

  const int32_t block_width = isyntax->block_width;
  const int32_t block_height = isyntax->block_height;
  const int32_t quadrant_width = block_width + pad_l_plus_r;
  const int32_t quadrant_height = block_height + pad_l_plus_r;
  const int32_t full_width = 2 * quadrant_width;

  icoeff_t* idwt = dest_buffer;
  const int32_t dest_stride = full_width;

  // Legacy behavior: initialize top-left quadrant as "white" for Y channel
  // only.
  if (color == 0) {
    for (int32_t x = 0; x < quadrant_width; ++x) {
      idwt[x] = 255;
    }
    for (int32_t y = 1; y < quadrant_width; ++y) {
      std::memcpy(idwt + y * dest_stride, idwt,
                  static_cast<size_t>(quadrant_width) * sizeof(icoeff_t));
    }
  }

  icoeff_t* h_dummy_coeff = isyntax->black_dummy_coeff;
  icoeff_t* ll_dummy_coeff =
      (color == 0) ? isyntax->white_dummy_coeff : isyntax->black_dummy_coeff;

  const int32_t block_stride = block_width * block_height;

  const std::array<int32_t, 4> quadrant_offsets = {
      0,
      quadrant_width,
      full_width * quadrant_height,
      full_width * quadrant_height + quadrant_width,
  };
  std::array<icoeff_t*, 4> quadrants{};
  for (int i = 0; i < 4; ++i) {
    quadrants[i] = idwt + quadrant_offsets[i];
  }

  uint32_t invalid_neighbors_ll = 0;
  uint32_t invalid_neighbors_h = 0;

  TileProcessingContext ctx;
  ctx.isyntax = isyntax;
  ctx.wsi = wsi;
  ctx.level = level;
  ctx.scale = scale;
  ctx.tile_x = tile_x;
  ctx.tile_y = tile_y;
  ctx.color = color;
  ctx.h_dummy_coeff = h_dummy_coeff;
  ctx.ll_dummy_coeff = ll_dummy_coeff;
  ctx.block_width = block_width;
  ctx.block_height = block_height;
  ctx.block_stride = block_stride;
  ctx.dest_stride = dest_stride;
  ctx.quadrants = quadrants;
  ctx.adj_tiles_mask = adj_tiles;
  ctx.invalid_neighbors_ll = &invalid_neighbors_ll;
  ctx.invalid_neighbors_h = &invalid_neighbors_h;

  for (int32_t dy = -1; dy <= 1; ++dy) {
    for (int32_t dx = -1; dx <= 1; ++dx) {
      ProcessNeighborTile(ctx, dy, dx);
    }
  }

  const size_t idwt_elems = static_cast<size_t>(quadrant_width * 2) *
                            static_cast<size_t>(quadrant_height * 2);
  ::isyntax::dwt::Idwt53(std::span<icoeff_t>(idwt, idwt_elems), quadrant_width,
                         quadrant_height);

  const uint32_t invalid_edges = invalid_neighbors_h | invalid_neighbors_ll;
  return invalid_edges;
}

}  // namespace tile
}  // namespace isyntax

namespace isyntax {
namespace tile {

uint32_t GetAdjacentTilesMask(const isyntax_level_t* level, int32_t tile_x,
                              int32_t tile_y) {
  ASSERT(level != nullptr);
  ASSERT(tile_x >= 0 && tile_y >= 0);
  ASSERT(tile_x < level->width_in_tiles && tile_y < level->height_in_tiles);

  uint32_t adj_tiles = 0x1FF;  // all bits set
  if (tile_y == 0) {
    adj_tiles &= ~(ISYNTAX_ADJ_TILE_TOP_LEFT | ISYNTAX_ADJ_TILE_TOP_CENTER |
                   ISYNTAX_ADJ_TILE_TOP_RIGHT);
  }
  if (tile_y == level->height_in_tiles - 1) {
    adj_tiles &=
        ~(ISYNTAX_ADJ_TILE_BOTTOM_LEFT | ISYNTAX_ADJ_TILE_BOTTOM_CENTER |
          ISYNTAX_ADJ_TILE_BOTTOM_RIGHT);
  }
  if (tile_x == 0) {
    adj_tiles &= ~(ISYNTAX_ADJ_TILE_TOP_LEFT | ISYNTAX_ADJ_TILE_CENTER_LEFT |
                   ISYNTAX_ADJ_TILE_BOTTOM_LEFT);
  }
  if (tile_x == level->width_in_tiles - 1) {
    adj_tiles &= ~(ISYNTAX_ADJ_TILE_TOP_RIGHT | ISYNTAX_ADJ_TILE_CENTER_RIGHT |
                   ISYNTAX_ADJ_TILE_BOTTOM_RIGHT);
  }
  return adj_tiles;
}

uint32_t GetAdjacentTilesMaskOnlyExisting(const isyntax_level_t* level,
                                          int32_t tile_x, int32_t tile_y) {
  const uint32_t adjacent = GetAdjacentTilesMask(level, tile_x, tile_y);
  uint32_t mask = 0;

  const auto maybe_set = [&](uint32_t bit, int32_t x, int32_t y) {
    if ((adjacent & bit) == 0) {
      return;
    }
    const isyntax_tile_t* tile = level->tiles + y * level->width_in_tiles + x;
    if (tile->exists) {
      mask |= bit;
    }
  };

  maybe_set(ISYNTAX_ADJ_TILE_TOP_LEFT, tile_x - 1, tile_y - 1);
  maybe_set(ISYNTAX_ADJ_TILE_TOP_CENTER, tile_x, tile_y - 1);
  maybe_set(ISYNTAX_ADJ_TILE_TOP_RIGHT, tile_x + 1, tile_y - 1);
  maybe_set(ISYNTAX_ADJ_TILE_CENTER_LEFT, tile_x - 1, tile_y);
  maybe_set(ISYNTAX_ADJ_TILE_CENTER, tile_x, tile_y);
  maybe_set(ISYNTAX_ADJ_TILE_CENTER_RIGHT, tile_x + 1, tile_y);
  maybe_set(ISYNTAX_ADJ_TILE_BOTTOM_LEFT, tile_x - 1, tile_y + 1);
  maybe_set(ISYNTAX_ADJ_TILE_BOTTOM_CENTER, tile_x, tile_y + 1);
  maybe_set(ISYNTAX_ADJ_TILE_BOTTOM_RIGHT, tile_x + 1, tile_y + 1);
  return mask;
}

}  // namespace tile
}  // namespace isyntax

namespace {

void CopyLlBlockFromIdwt(icoeff_t* dest, int32_t dest_stride,
                         const icoeff_t* idwt, int32_t idwt_stride,
                         int32_t src_x, int32_t src_y, int32_t block_width,
                         int32_t block_height) {
  const size_t row_copy_size =
      static_cast<size_t>(block_width) * sizeof(icoeff_t);
  const icoeff_t* source = idwt + src_y * idwt_stride + src_x;
  for (int32_t y = 0; y < block_height; ++y) {
    std::memcpy(dest, source, row_copy_size);
    dest += dest_stride;
    source += idwt_stride;
  }
}

aifocore::Status DistributeLlToChildrenFromIdwtImpl(
    isyntax_image_t* wsi, int32_t scale, int32_t tile_x, int32_t tile_y,
    int32_t color, const icoeff_t* idwt, int32_t idwt_stride,
    int32_t first_valid_pixel, int32_t block_width, int32_t block_height,
    block_allocator_t* ll_coeff_block_allocator) {
  if (wsi == nullptr || idwt == nullptr ||
      ll_coeff_block_allocator == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "DistributeLlToChildrenFromIdwt: null input");
  }
  if (scale <= 0) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        "DistributeLlToChildrenFromIdwt: scale must be > 0");
  }

  isyntax_level_t* next_level = wsi->levels + (scale - 1);
  isyntax_tile_t* child_top_left = next_level->tiles +
                                   (tile_y * 2) * next_level->width_in_tiles +
                                   (tile_x * 2);
  isyntax_tile_t* child_top_right = child_top_left + 1;
  isyntax_tile_t* child_bottom_left =
      child_top_left + next_level->width_in_tiles;
  isyntax_tile_t* child_bottom_right = child_bottom_left + 1;

  auto& tl_chan = child_top_left->color_channels[color];
  auto& tr_chan = child_top_right->color_channels[color];
  auto& bl_chan = child_bottom_left->color_channels[color];
  auto& br_chan = child_bottom_right->color_channels[color];

  // Free existing blocks (legacy behavior).
  if (tl_chan.coeff_ll) {
    block_free(ll_coeff_block_allocator, tl_chan.coeff_ll);
    tl_chan.coeff_ll = nullptr;
  }
  if (tr_chan.coeff_ll) {
    block_free(ll_coeff_block_allocator, tr_chan.coeff_ll);
    tr_chan.coeff_ll = nullptr;
  }
  if (bl_chan.coeff_ll) {
    block_free(ll_coeff_block_allocator, bl_chan.coeff_ll);
    bl_chan.coeff_ll = nullptr;
  }
  if (br_chan.coeff_ll) {
    block_free(ll_coeff_block_allocator, br_chan.coeff_ll);
    br_chan.coeff_ll = nullptr;
  }

  // Allocate new blocks.
  tl_chan.coeff_ll =
      static_cast<icoeff_t*>(block_alloc(ll_coeff_block_allocator));
  tr_chan.coeff_ll =
      static_cast<icoeff_t*>(block_alloc(ll_coeff_block_allocator));
  bl_chan.coeff_ll =
      static_cast<icoeff_t*>(block_alloc(ll_coeff_block_allocator));
  br_chan.coeff_ll =
      static_cast<icoeff_t*>(block_alloc(ll_coeff_block_allocator));

  if (tl_chan.coeff_ll == nullptr || tr_chan.coeff_ll == nullptr ||
      bl_chan.coeff_ll == nullptr || br_chan.coeff_ll == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                            "Failed to allocate LL blocks for children");
  }

  const int32_t dest_stride = block_width;
  const int32_t src0_x = first_valid_pixel;
  const int32_t src0_y = first_valid_pixel;

  CopyLlBlockFromIdwt(tl_chan.coeff_ll, dest_stride, idwt, idwt_stride, src0_x,
                      src0_y, block_width, block_height);
  CopyLlBlockFromIdwt(tr_chan.coeff_ll, dest_stride, idwt, idwt_stride,
                      src0_x + block_width, src0_y, block_width, block_height);
  CopyLlBlockFromIdwt(bl_chan.coeff_ll, dest_stride, idwt, idwt_stride, src0_x,
                      src0_y + block_height, block_width, block_height);
  CopyLlBlockFromIdwt(br_chan.coeff_ll, dest_stride, idwt, idwt_stride,
                      src0_x + block_width, src0_y + block_height, block_width,
                      block_height);

  return aifocore::Status::OkStatus();
}

}  // namespace

namespace isyntax {
namespace tile {

aifocore::Status DistributeLlToChildrenFromIdwt(
    isyntax_image_t* wsi, int32_t scale, int32_t tile_x, int32_t tile_y,
    int32_t color, const icoeff_t* idwt, int32_t idwt_stride,
    int32_t first_valid_pixel, int32_t block_width, int32_t block_height,
    block_allocator_t* ll_coeff_block_allocator) {
  return DistributeLlToChildrenFromIdwtImpl(
      wsi, scale, tile_x, tile_y, color, idwt, idwt_stride, first_valid_pixel,
      block_width, block_height, ll_coeff_block_allocator);
}

namespace {

class TempMemoryGuard {
 public:
  TempMemoryGuard() : mem_(begin_temp_memory_on_local_thread()) {}

  TempMemoryGuard(const TempMemoryGuard&) = delete;
  TempMemoryGuard& operator=(const TempMemoryGuard&) = delete;

  ~TempMemoryGuard() { release_temp_memory(&mem_); }

  arena_t* arena() { return mem_.arena; }

 private:
  temp_memory_t mem_;
};

aifocore::Status LoadTileImpl(isyntax_t* isyntax, isyntax_image_t* wsi,
                              int32_t scale, int32_t tile_x, int32_t tile_y,
                              block_allocator_t* ll_coeff_block_allocator,
                              uint32_t* out_buffer_or_null,
                              enum isyntax_pixel_format_t pixel_format) {
  if (isyntax == nullptr || wsi == nullptr ||
      ll_coeff_block_allocator == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "isyntax_load_tile: null input");
  }

  isyntax_level_t* level = wsi->levels + scale;
  ASSERT(tile_x >= 0 && tile_x < level->width_in_tiles);
  ASSERT(tile_y >= 0 && tile_y < level->height_in_tiles);
  isyntax_tile_t* tile = level->tiles + tile_y * level->width_in_tiles + tile_x;

  const int32_t block_width = isyntax->block_width;
  const int32_t block_height = isyntax->block_height;
  const int32_t first_valid_pixel = ISYNTAX_IDWT_FIRST_VALID_PIXEL;
  const int32_t idwt_width =
      2 * (block_width + ISYNTAX_IDWT_PAD_L + ISYNTAX_IDWT_PAD_R);
  const int32_t idwt_height =
      2 * (block_height + ISYNTAX_IDWT_PAD_L + ISYNTAX_IDWT_PAD_R);
  const int32_t idwt_stride = idwt_width;

  TempMemoryGuard temp;

  icoeff_t* Y = nullptr;
  icoeff_t* Co = nullptr;
  icoeff_t* Cg = nullptr;

  uint32_t invalid_edges = 0;

  for (int32_t color = 0; color < 3; ++color) {
    const size_t idwt_buffer_size = static_cast<size_t>(idwt_width) *
                                    static_cast<size_t>(idwt_height) *
                                    sizeof(icoeff_t);
    icoeff_t* idwt =
        static_cast<icoeff_t*>(arena_push_size(temp.arena(), idwt_buffer_size));
    std::memset(idwt, 0, idwt_buffer_size);

    AIFOCORE_ASSIGN_OR_RETURN(
        const uint32_t invalid_for_color,
        ::isyntax::tile::IdwtTileForColorChannel(isyntax, wsi, scale, tile_x,
                                                 tile_y, color, idwt));
    invalid_edges |= invalid_for_color;
    ASSERT(idwt);

    if (color == 0) {
      Y = idwt;
    } else if (color == 1) {
      Co = idwt;
    } else {
      Cg = idwt;
    }

    if (scale > 0) {
      AIFOCORE_RETURN_IF_ERROR(DistributeLlToChildrenFromIdwt(
          wsi, scale, tile_x, tile_y, color, idwt, idwt_stride,
          first_valid_pixel, block_width, block_height,
          ll_coeff_block_allocator));
    }

    if (color == 2 && scale > 0) {
      isyntax_level_t* next_level = wsi->levels + (scale - 1);
      isyntax_tile_t* child_top_left =
          next_level->tiles + (tile_y * 2) * next_level->width_in_tiles +
          (tile_x * 2);
      isyntax_tile_t* child_top_right = child_top_left + 1;
      isyntax_tile_t* child_bottom_left =
          child_top_left + next_level->width_in_tiles;
      isyntax_tile_t* child_bottom_right = child_bottom_left + 1;

      child_top_left->has_ll = true;
      child_top_right->has_ll = true;
      child_bottom_left->has_ll = true;
      child_bottom_right->has_ll = true;

      if (invalid_edges != 0) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            aifocore::fmt::format("load: scale={} x={} y={} invalid edges={:x}",
                                  scale, tile_x, tile_y, invalid_edges));
      }
    }
  }

  tile->is_loaded = true;
  if (out_buffer_or_null == nullptr) {
    return aifocore::Status::OkStatus();
  }

  // Y is stored in signed-magnitude form; convert to absolute.
  isyntax::color::SignedMagnitudeToAbsoluteValue16Block(
      reinterpret_cast<int16_t*>(Y),
      static_cast<uint32_t>(idwt_width * idwt_height));

  const int32_t tile_width = block_width * 2;
  const int32_t tile_height = block_height * 2;
  const int32_t valid_offset =
      (first_valid_pixel * idwt_stride) + first_valid_pixel;

  switch (pixel_format) {
    case ISYNTAX_PIXEL_FORMAT_BGRA:
      isyntax::color::ConvertYCoCgToBgraBlock(
          reinterpret_cast<int16_t*>(Y + valid_offset),
          reinterpret_cast<int16_t*>(Co + valid_offset),
          reinterpret_cast<int16_t*>(Cg + valid_offset), tile_width,
          tile_height, idwt_stride, out_buffer_or_null);
      break;
    case ISYNTAX_PIXEL_FORMAT_RGBA:
      isyntax::color::ConvertYCoCgToRgbaBlock(
          reinterpret_cast<int16_t*>(Y + valid_offset),
          reinterpret_cast<int16_t*>(Co + valid_offset),
          reinterpret_cast<int16_t*>(Cg + valid_offset), tile_width,
          tile_height, idwt_stride, out_buffer_or_null);
      break;
    default:
      ASSERT(!"unknown pixel format!");
      break;
  }

  return aifocore::Status::OkStatus();
}

}  // namespace

aifocore::Status LoadTile(isyntax_t* isyntax, isyntax_image_t* wsi,
                          int32_t scale, int32_t tile_x, int32_t tile_y,
                          block_allocator_t* ll_coeff_block_allocator,
                          uint32_t* out_buffer_or_null,
                          enum isyntax_pixel_format_t pixel_format) {
  return LoadTileImpl(isyntax, wsi, scale, tile_x, tile_y,
                      ll_coeff_block_allocator, out_buffer_or_null,
                      pixel_format);
}

}  // namespace tile
}  // namespace isyntax
