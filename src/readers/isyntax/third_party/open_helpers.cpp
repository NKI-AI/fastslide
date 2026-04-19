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

#include "fastslide/readers/isyntax/third_party/open_helpers.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/chunk_layout.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/utils/block_allocator.h"

namespace {

template <typename T>
using MallocPtr = std::unique_ptr<T, decltype(&std::free)>;

template <typename T>
MallocPtr<T> CallocArray(size_t count) {
  void* p = std::calloc(count, sizeof(T));
  return MallocPtr<T>(static_cast<T*>(p), &std::free);
}

template <typename T>
MallocPtr<T> MallocStruct() {
  void* p = std::malloc(sizeof(T));
  return MallocPtr<T>(static_cast<T*>(p), &std::free);
}

inline int32_t GetFirstValidCoefPixel(int32_t scale) {
  // see docs in `dwt.h`
  int32_t result = (3 << scale) - 2;
  return result;
}

inline int32_t GetFirstValidLlPixel(int32_t scale) {
  int32_t result = GetFirstValidCoefPixel(scale) + (1 << scale);
  return result;
}

}  // namespace

namespace isyntax {
namespace open {

void InitializeLevelGeometry(isyntax_t* isyntax, isyntax_image_t* wsi_image) {
  if (isyntax->mpp_x <= 0.0f || isyntax->mpp_y <= 0.0f) {
    // Should usually be 0.25; zero or below can never be right.
    isyntax->mpp_x = 1.0f;
    isyntax->mpp_y = 1.0f;
    isyntax->is_mpp_known = false;
  }

  isyntax->block_width = isyntax->block_header_templates[0].block_width;
  isyntax->block_height = isyntax->block_header_templates[0].block_height;

  // Tile dimension AFTER inverse wavelet transform.
  isyntax->tile_width = isyntax->block_width * 2;
  isyntax->tile_height = isyntax->block_height * 2;

  const int32_t block_width = isyntax->block_width;
  const int32_t block_height = isyntax->block_height;
  const int32_t tile_width = isyntax->tile_width;
  const int32_t tile_height = isyntax->tile_height;

  const int32_t num_levels = wsi_image->level_count;
  ASSERT(num_levels >= 1);
  const int32_t grid_width =
      ((wsi_image->width_including_padding + (block_width << num_levels) - 1) /
       (block_width << num_levels))
      << (num_levels - 1);
  const int32_t grid_height = ((wsi_image->height_including_padding +
                                (block_height << num_levels) - 1) /
                               (block_height << num_levels))
                              << (num_levels - 1);

  const int32_t base_level_tile_count = grid_height * grid_width;
  for (int32_t scale = 0; scale < wsi_image->level_count; ++scale) {
    isyntax_level_t* level = wsi_image->levels + scale;
    level->tile_count = base_level_tile_count >> (scale * 2);
    level->scale = scale;
    level->width_in_tiles = grid_width >> scale;
    level->height_in_tiles = grid_height >> scale;
    level->width = wsi_image->width >> scale;
    level->height = wsi_image->height >> scale;
    level->downsample_factor = static_cast<float>(1 << scale);
    level->um_per_pixel_x = isyntax->mpp_x * level->downsample_factor;
    level->um_per_pixel_y = isyntax->mpp_y * level->downsample_factor;
    level->x_tile_side_in_um =
        static_cast<float>(tile_width) * level->um_per_pixel_x;
    level->y_tile_side_in_um =
        static_cast<float>(tile_height) * level->um_per_pixel_y;
  }

  // When recursively decoding the tiles, at each iteration the image is
  // slightly offset to the top left. The shift corresponds to the per
  // level padding added for the wavelet transform:
  // ((3 << (scale-1)) - 2)
  // Put another way: the highest (zoomed out levels) are shifted the to
  // the bottom right (this is also reflected in the x and y coordinates
  // of the codeblocks in the iSyntax header). Level 0 has no offset.
  for (int32_t scale = 1; scale < wsi_image->level_count; ++scale) {
    isyntax_level_t* level = wsi_image->levels + scale;
    level->origin_offset_in_pixels = GetFirstValidCoefPixel(scale - 1);

    const float offset_in_um_x =
        static_cast<float>(level->origin_offset_in_pixels) *
        wsi_image->levels[0].um_per_pixel_x;
    const float offset_in_um_y =
        static_cast<float>(level->origin_offset_in_pixels) *
        wsi_image->levels[0].um_per_pixel_y;
    level->origin_offset = (v2f){offset_in_um_x - 1.5f, offset_in_um_y - 1.5f};
  }
}

void ProcessCodeBlocks(isyntax_t* isyntax, isyntax_image_t* wsi_image) {
  const int32_t tile_width = isyntax->tile_width;
  const int32_t tile_height = isyntax->tile_height;
  const int32_t num_levels = wsi_image->level_count;
  const int32_t block_width = isyntax->block_width;
  const int32_t block_height = isyntax->block_height;

  const int32_t grid_width =
      ((wsi_image->width_including_padding + (block_width << num_levels) - 1) /
       (block_width << num_levels))
      << (num_levels - 1);

  // The highest level has LL tiles in addition to LH/HL/HH tiles.
  int64_t total_coeff_tile_count = 0;
  for (int32_t scale = 0; scale < wsi_image->level_count; ++scale) {
    const isyntax_level_t* level = wsi_image->levels + scale;
    total_coeff_tile_count += level->tile_count;
  }
  const int64_t base_level_tile_count =
      static_cast<int64_t>(grid_width) *
      static_cast<int64_t>(((wsi_image->height_including_padding +
                             (block_height << num_levels) - 1) /
                            (block_height << num_levels))
                           << (num_levels - 1));
  const int64_t ll_coeff_tile_count =
      base_level_tile_count >> ((num_levels - 1) * 2);
  total_coeff_tile_count += ll_coeff_tile_count;

  for (int32_t i = 0; i < wsi_image->codeblock_count; ++i) {
    isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;

    // Calculate adjusted codeblock coordinates so that they fit the origin of
    // the image.
    codeblock->x_adjusted =
        static_cast<int32_t>(codeblock->x_coordinate) - wsi_image->offset_x;
    codeblock->y_adjusted =
        static_cast<int32_t>(codeblock->y_coordinate) - wsi_image->offset_y;

    // Calculate the block ID (= index into the seektable) adapted from
    // extract_block_header.py.
    const bool is_ll = codeblock->coefficient == 0;
    uint32_t block_id = 0;
    const int32_t maxscale = is_ll ? codeblock->scale + 1 : codeblock->scale;
    for (int32_t scale = 0; scale < maxscale; ++scale) {
      block_id += wsi_image->levels[scale].tile_count;
    }

    const int32_t offset = is_ll ? GetFirstValidLlPixel(codeblock->scale)
                                 : GetFirstValidCoefPixel(codeblock->scale);
    const int32_t x = codeblock->x_adjusted - offset;
    const int32_t y = codeblock->y_adjusted - offset;
    codeblock->block_x = x / (tile_width << codeblock->scale);
    codeblock->block_y = y / (tile_height << codeblock->scale);

    const int32_t grid_stride = grid_width >> codeblock->scale;
    block_id += codeblock->block_y * grid_stride + codeblock->block_x;

    const int32_t tiles_per_color =
        static_cast<int32_t>(total_coeff_tile_count);
    block_id += codeblock->color_component * tiles_per_color;
    codeblock->block_id = block_id;
  }
}

aifocore::Status CreateTileLookupTables(isyntax_t* isyntax,
                                        isyntax_image_t* wsi_image) {
  // Allocate enough space for the maximum number of codeblock 'chunks' we can
  // expect (the actual number of chunks may be lower, because some tiles might
  // not exist).
  int32_t max_possible_chunk_count = 0;
  for (int32_t scale = 0; scale <= wsi_image->max_scale; ++scale) {
    if ((scale + 1) % 3 == 0 || scale == wsi_image->max_scale) {
      isyntax_level_t* level = wsi_image->levels + scale;
      max_possible_chunk_count += level->tile_count;
    }
  }
  if (max_possible_chunk_count <= 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("iSyntax: invalid max_possible_chunk_count={}",
                              max_possible_chunk_count));
  }

  MallocPtr<isyntax_data_chunk_t> data_chunks =
      CallocArray<isyntax_data_chunk_t>(
          static_cast<size_t>(max_possible_chunk_count));
  if (data_chunks == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kResourceExhausted,
                                "iSyntax: failed to allocate data_chunks");
  }

  std::vector<MallocPtr<isyntax_tile_t>> tiles_per_level;
  tiles_per_level.reserve(static_cast<size_t>(wsi_image->level_count));
  for (int32_t i = 0; i < wsi_image->level_count; ++i) {
    isyntax_level_t* level = wsi_image->levels + i;
    MallocPtr<isyntax_tile_t> tiles =
        CallocArray<isyntax_tile_t>(static_cast<size_t>(level->tile_count));
    if (tiles == nullptr) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kResourceExhausted,
                                  "iSyntax: failed to allocate tiles");
    }
    tiles_per_level.push_back(std::move(tiles));
  }

  // Temporarily attach owned buffers to `wsi_image` while we populate them.
  wsi_image->data_chunks = data_chunks.get();
  for (int32_t i = 0; i < wsi_image->level_count; ++i) {
    wsi_image->levels[i].tiles = tiles_per_level[static_cast<size_t>(i)].get();
  }
  wsi_image->data_chunk_count = 0;

  int32_t current_chunk_codeblock_index = 0;
  int32_t next_chunk_codeblock_index = 0;
  int32_t current_data_chunk_index = 0;
  int32_t next_data_chunk_index = 0;
  for (int32_t i = 0; i < wsi_image->codeblock_count; ++i) {
    isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
    if (codeblock->color_component != 0) {
      // Don't let color channels 1 and 2 overwrite what was already set.
      i = next_chunk_codeblock_index;  // skip ahead
      if (i >= wsi_image->codeblock_count) {
        break;
      }
      codeblock = wsi_image->codeblocks + i;
    }

    // Keep track of where we are in the 'chunk' of codeblocks.
    if (i == next_chunk_codeblock_index) {
      // This codeblock is the top of a new chunk.
      int32_t chunk_codeblock_count_per_color = 0;
      if (codeblock->scale == wsi_image->max_scale) {
        chunk_codeblock_count_per_color =
            isyntax::chunk::GetChunkCodeblocksPerColorForLevel(codeblock->scale,
                                                               true);
      } else {
        chunk_codeblock_count_per_color = 21;
      }

      current_chunk_codeblock_index = i;
      next_chunk_codeblock_index = i + (chunk_codeblock_count_per_color * 3);
      current_data_chunk_index = next_data_chunk_index;
      if (current_data_chunk_index >= max_possible_chunk_count) {
        // Roll back pointers so the failure path doesn't hold dangling memory.
        wsi_image->data_chunks = nullptr;
        for (int32_t j = 0; j < wsi_image->level_count; ++j) {
          wsi_image->levels[j].tiles = nullptr;
        }
        wsi_image->data_chunk_count = 0;
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            "iSyntax: encountered too many data chunks");
      }

      isyntax_data_chunk_t* chunk =
          wsi_image->data_chunks + current_data_chunk_index;
      chunk->offset = codeblock->block_data_offset;
      chunk->top_codeblock_index = current_chunk_codeblock_index;
      chunk->codeblock_count_per_color = chunk_codeblock_count_per_color;
      chunk->scale = codeblock->scale;
      ++wsi_image->data_chunk_count;
      ++next_data_chunk_index;
    }

    isyntax_level_t* level = wsi_image->levels + codeblock->scale;
    const int32_t tile_index =
        codeblock->block_y * level->width_in_tiles + codeblock->block_x;
    ASSERT(tile_index < level->tile_count);
    level->tiles[tile_index].exists = true;
    level->tiles[tile_index].codeblock_index = i;
    level->tiles[tile_index].codeblock_chunk_index =
        current_chunk_codeblock_index;
    level->tiles[tile_index].data_chunk_index = current_data_chunk_index;
  }

  // Success: publish ownership to `wsi_image`/levels.
  (void)data_chunks.release();
  for (int32_t i = 0; i < wsi_image->level_count; ++i) {
    (void)tiles_per_level[static_cast<size_t>(i)].release();
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status InitializeBlockAllocators(isyntax_t* isyntax,
                                           isyntax_open_flags_t flags) {
  if (isyntax == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "InitializeBlockAllocators: null isyntax");
  }
  const size_t ll_coeff_block_size =
      static_cast<size_t>(isyntax->block_width) *
      static_cast<size_t>(isyntax->block_height) * sizeof(icoeff_t);
  if (ll_coeff_block_size == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "InitializeBlockAllocators: invalid block size");
  }

  const size_t block_allocator_maximum_capacity_in_blocks =
      GIGABYTES(32) / ll_coeff_block_size;
  const size_t ll_coeff_block_allocator_capacity_in_blocks =
      block_allocator_maximum_capacity_in_blocks / 4;
  const size_t h_coeff_block_size = ll_coeff_block_size * 3;
  const size_t h_coeff_block_allocator_capacity_in_blocks =
      ll_coeff_block_allocator_capacity_in_blocks * 3;

  if (flags & ISYNTAX_OPEN_FLAG_INIT_ALLOCATORS) {
    isyntax->ll_coeff_block_allocator = block_allocator_create(
        ll_coeff_block_size, ll_coeff_block_allocator_capacity_in_blocks,
        MEGABYTES(256));
    isyntax->h_coeff_block_allocator = block_allocator_create(
        h_coeff_block_size, h_coeff_block_allocator_capacity_in_blocks,
        MEGABYTES(256));
    if (isyntax->ll_coeff_block_allocator == nullptr ||
        isyntax->h_coeff_block_allocator == nullptr) {
      if (isyntax->ll_coeff_block_allocator != nullptr) {
        block_allocator_destroy(isyntax->ll_coeff_block_allocator);
        isyntax->ll_coeff_block_allocator = nullptr;
      }
      if (isyntax->h_coeff_block_allocator != nullptr) {
        block_allocator_destroy(isyntax->h_coeff_block_allocator);
        isyntax->h_coeff_block_allocator = nullptr;
      }
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kResourceExhausted,
          "InitializeBlockAllocators: allocator create failed");
    }
    isyntax->is_block_allocator_owned = true;
  } else {
    // The caller must inject the allocators after return of isyntax_open().
    isyntax->ll_coeff_block_allocator = NULL;
    isyntax->h_coeff_block_allocator = NULL;
    isyntax->is_block_allocator_owned = false;
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status InitializeDummyBlocks(isyntax_t* isyntax) {
  if (isyntax == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "InitializeDummyBlocks: null isyntax");
  }
  const size_t coeff_count = static_cast<size_t>(isyntax->block_width) *
                             static_cast<size_t>(isyntax->block_height);
  const size_t bytes = coeff_count * sizeof(icoeff_t);

  // Initialize dummy blocks with 'background' coefficients, to use for filling
  // in margins at the edges (in case the neighboring codeblock doesn't exist).
  if (!isyntax->black_dummy_coeff) {
    MallocPtr<icoeff_t> black(
        static_cast<icoeff_t*>(std::calloc(coeff_count, sizeof(icoeff_t))),
        &std::free);
    if (black == nullptr) {
      return aifocore::Status(
          aifocore::StatusCode::kResourceExhausted,
          "InitializeDummyBlocks: calloc black_dummy_coeff failed");
    }
    isyntax->black_dummy_coeff = black.release();
  }
  if (!isyntax->white_dummy_coeff) {
    MallocPtr<icoeff_t> white(static_cast<icoeff_t*>(std::malloc(bytes)),
                              &std::free);
    if (white == nullptr) {
      return aifocore::Status(
          aifocore::StatusCode::kResourceExhausted,
          "InitializeDummyBlocks: malloc white_dummy_coeff failed");
    }
    std::fill_n(white.get(), coeff_count, static_cast<icoeff_t>(255));
    isyntax->white_dummy_coeff = white.release();
  }
  return aifocore::Status::OkStatus();
}

}  // namespace open
}  // namespace isyntax
