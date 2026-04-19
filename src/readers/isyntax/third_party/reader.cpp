/*
  BSD 2-Clause License

  Copyright (c) 2019-2025, Pieter Valkema, Alexandr Virodov

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

// This file ports `isyntax_reader.c` to C++20 while keeping the existing C ABI
// from `reader.h`.

#include "fastslide/readers/isyntax/third_party/reader.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#include "aifocore/platform/portability.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/decompress.h"
#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/platform/platform.h"
#include "fastslide/readers/isyntax/third_party/tile.h"
#include "fastslide/readers/isyntax/third_party/utils/block_allocator.h"

namespace {

template <typename T>
using MallocPtr = std::unique_ptr<T, decltype(&std::free)>;

template <typename T>
MallocPtr<T> MallocBytes(size_t bytes) {
  void* p = std::malloc(bytes);
  return MallocPtr<T>(static_cast<T*>(p), &std::free);
}

inline void FillWhiteTile(isyntax_t* isyntax, uint32_t* pixels_buffer) {
  if (isyntax == nullptr || pixels_buffer == nullptr) {
    return;
  }
  const size_t pixel_count = static_cast<size_t>(isyntax->tile_width) *
                             static_cast<size_t>(isyntax->tile_height);
  std::memset(pixels_buffer, 0xFF, pixel_count * sizeof(uint32_t));
}

#define ITERATE_TILE_LIST(_iter, _list) \
  isyntax_tile_t* _iter = (_list).head; \
  _iter;                                \
  _iter = _iter->cache_next

static void TileListInsertFirst(isyntax_tile_list_t* list,
                                isyntax_tile_t* tile) {
  ASSERT(list != nullptr);
  ASSERT(tile != nullptr);
  ASSERT(tile->cache_next == NULL && tile->cache_prev == NULL);
  if (list->head == NULL) {
    list->head = tile;
    list->tail = tile;
  } else {
    list->head->cache_prev = tile;
    tile->cache_next = list->head;
    list->head = tile;
  }
  list->count++;
}

static void TileListInsertListFirst(isyntax_tile_list_t* target_list,
                                    isyntax_tile_list_t* source_list) {
  ASSERT(target_list != nullptr);
  ASSERT(source_list != nullptr);
  if (source_list->head == NULL && source_list->tail == NULL) {
    return;
  }

  source_list->tail->cache_next = target_list->head;
  if (target_list->head) {
    target_list->head->cache_prev = source_list->tail;
  }

  target_list->head = source_list->head;
  if (target_list->tail == NULL) {
    target_list->tail = source_list->tail;
  }
  target_list->count += source_list->count;
  source_list->head = NULL;
  source_list->tail = NULL;
  source_list->count = 0;
}

aifocore::Status OpenslideLoadTileCoefficientsLlOrH(isyntax_cache_t* cache,
                                                    isyntax_t* isyntax,
                                                    isyntax_tile_t* tile,
                                                    int codeblock_index,
                                                    bool is_ll) {
  if (!cache || !isyntax || !tile) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "OpenslideLoadTileCoefficientsLlOrH: null argument "
            "(cache={} isyntax={} tile={})",
            static_cast<const void*>(cache), static_cast<const void*>(isyntax),
            static_cast<const void*>(tile)));
  }

  // Ensure thread-local memory is initialized (goroutines may hop threads).
  if (!local_thread_memory) {
    init_thread_memory(0, &global_system_info);
  }

  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  isyntax_data_chunk_t* chunk = &wsi->data_chunks[tile->data_chunk_index];

  for (int color = 0; color < 3; ++color) {
    isyntax_codeblock_t* codeblock =
        &wsi->codeblocks[codeblock_index +
                         color * chunk->codeblock_count_per_color];
    ASSERT(codeblock->coefficient == (is_ll ? 0 : 1));
    ASSERT(codeblock->color_component == (uint32_t)color);
    ASSERT(codeblock->scale == (uint32_t)tile->tile_scale);

    if (is_ll) {
      if (!cache->ll_coeff_block_allocator) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                    "LL coeff block allocator is null");
      }
      auto coeff_or =
          isyntax::alloc::BlockAlloc(cache->ll_coeff_block_allocator);
      if (!coeff_or.ok()) {
        return coeff_or.status();
      }
      tile->color_channels[color].coeff_ll = static_cast<icoeff_t*>(*coeff_or);
    } else {
      if (!cache->h_coeff_block_allocator) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                    "H coeff block allocator is null");
      }
      auto coeff_or =
          isyntax::alloc::BlockAlloc(cache->h_coeff_block_allocator);
      if (!coeff_or.ok()) {
        return coeff_or.status();
      }
      tile->color_channels[color].coeff_h = static_cast<icoeff_t*>(*coeff_or);
    }

    // Add 7 safety bytes so the decompressor's 64-bit bitstream reads won't
    // read past the allocation.
    auto codeblock_data =
        MallocBytes<uint8_t>(static_cast<size_t>(codeblock->block_size) + 7);
    if (!codeblock_data) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kResourceExhausted,
          aifocore::fmt::format("Failed to allocate codeblock buffer (size={})",
                                codeblock->block_size));
    }

    const ssize_t bytes_read = aifocore::portable_pread(
        static_cast<int>(isyntax->file_handle), codeblock_data.get(),
        static_cast<size_t>(codeblock->block_size),
        static_cast<uint64_t>(codeblock->block_data_offset));
    if (bytes_read < 0 || static_cast<size_t>(bytes_read) !=
                              static_cast<size_t>(codeblock->block_size)) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kDataLoss,
          aifocore::fmt::format(
              "Failed to read iSyntax data (offset={} size={} read={})",
              codeblock->block_data_offset, codeblock->block_size, bytes_read));
    }

    // Zero the safety bytes so any speculative reads past `block_size` are
    // deterministic. (The decompressor reads 64-bit chunks from the bitstream.)
    std::memset(codeblock_data.get() + codeblock->block_size, 0, 7);

    const size_t expected_coeff_count =
        static_cast<size_t>((codeblock->coefficient == 1) ? 3 : 1) *
        static_cast<size_t>(isyntax->block_width) *
        static_cast<size_t>(isyntax->block_height);
    int16_t* out_ptr =
        reinterpret_cast<int16_t*>(is_ll ? tile->color_channels[color].coeff_ll
                                         : tile->color_channels[color].coeff_h);

    const aifocore::Status st = isyntax::HulskenDecompress(
        std::span<uint8_t>(reinterpret_cast<uint8_t*>(codeblock_data.get()),
                           static_cast<size_t>(codeblock->block_size)),
        isyntax->block_width, isyntax->block_height, codeblock->coefficient,
        wsi->compressor_version,
        std::span<int16_t>(out_ptr, expected_coeff_count));
    AIFOCORE_RETURN_IF_ERROR(st);
  }

  if (is_ll) {
    tile->has_ll = true;
  } else {
    tile->has_h = true;
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status OpenslideLoadTileCoefficients(isyntax_cache_t* cache,
                                               isyntax_t* isyntax,
                                               isyntax_tile_t* tile) {
  if (cache == nullptr || isyntax == nullptr || tile == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OpenslideLoadTileCoefficients: null argument");
  }
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  if (!tile->exists) {
    return aifocore::Status::OkStatus();
  }

  // Load LL codeblocks only for top-level tiles. For other levels, LL comes
  // from parent IDWT.
  if (!tile->has_ll && tile->tile_scale == wsi->max_scale) {
    AIFOCORE_RETURN_IF_ERROR(OpenslideLoadTileCoefficientsLlOrH(
        cache, isyntax, tile,
        /*codeblock_index=*/tile->codeblock_index,
        /*is_ll=*/true));
  }

  if (!tile->has_h) {
    ASSERT(tile->exists);
    isyntax_data_chunk_t* chunk = wsi->data_chunks + tile->data_chunk_index;

    const int32_t scale_in_chunk = chunk->scale - tile->tile_scale;
    ASSERT(scale_in_chunk >= 0 && scale_in_chunk < 3);
    int32_t codeblock_index_in_chunk = 0;
    if (scale_in_chunk == 0) {
      codeblock_index_in_chunk = 0;
    } else if (scale_in_chunk == 1) {
      codeblock_index_in_chunk =
          1 + (tile->tile_y % 2) * 2 + (tile->tile_x % 2);
    } else if (scale_in_chunk == 2) {
      codeblock_index_in_chunk =
          5 + (tile->tile_y % 4) * 4 + (tile->tile_x % 4);
    } else {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "Invalid scale_in_chunk");
    }

    AIFOCORE_RETURN_IF_ERROR(OpenslideLoadTileCoefficientsLlOrH(
        cache, isyntax, tile,
        /*codeblock_index=*/tile->codeblock_chunk_index +
            codeblock_index_in_chunk,
        /*is_ll=*/false));
  }

  return aifocore::Status::OkStatus();
}

typedef union isyntax_tile_children_t {
  struct {
    isyntax_tile_t* child_top_left;
    isyntax_tile_t* child_top_right;
    isyntax_tile_t* child_bottom_left;
    isyntax_tile_t* child_bottom_right;
  };

  isyntax_tile_t* as_array[4];
} isyntax_tile_children_t;

static isyntax_tile_children_t OpenslideComputeChildren(isyntax_t* isyntax,
                                                        isyntax_tile_t* tile) {
  isyntax_tile_children_t result;
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  ASSERT(tile->tile_scale > 0);
  isyntax_level_t* next_level = &wsi->levels[tile->tile_scale - 1];
  result.child_top_left = next_level->tiles +
                          (tile->tile_y * 2) * next_level->width_in_tiles +
                          (tile->tile_x * 2);
  result.child_top_right = result.child_top_left + 1;
  result.child_bottom_left = result.child_top_left + next_level->width_in_tiles;
  result.child_bottom_right = result.child_bottom_left + 1;
  return result;
}

aifocore::Status OpenslideIdwt(isyntax_cache_t* cache, isyntax_t* isyntax,
                               isyntax_tile_t* tile, uint32_t* pixels_buffer,
                               enum isyntax_pixel_format_t pixel_format) {
  if (cache == nullptr || isyntax == nullptr || tile == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OpenslideIdwt: null argument");
  }
  if (tile->tile_scale == 0) {
    ASSERT(pixels_buffer != NULL);
    const aifocore::Status st = isyntax::tile::LoadTile(
        isyntax, &isyntax->images[isyntax->wsi_image_index], tile->tile_scale,
        tile->tile_x, tile->tile_y, cache->ll_coeff_block_allocator,
        pixels_buffer, pixel_format);
    AIFOCORE_RETURN_IF_ERROR(st);
    return aifocore::Status::OkStatus();
  }

  if (pixels_buffer != NULL) {
    const aifocore::Status st = isyntax::tile::LoadTile(
        isyntax, &isyntax->images[isyntax->wsi_image_index], tile->tile_scale,
        tile->tile_x, tile->tile_y, cache->ll_coeff_block_allocator,
        pixels_buffer, pixel_format);
    AIFOCORE_RETURN_IF_ERROR(st);
    return aifocore::Status::OkStatus();
  }

  // If all children have ll coefficients and we don't need rgb pixels, we can
  // skip IDWT.
  ASSERT(pixels_buffer == NULL && tile->tile_scale > 0);
  isyntax_tile_children_t children = OpenslideComputeChildren(isyntax, tile);
  if (children.child_top_left->has_ll && children.child_top_right->has_ll &&
      children.child_bottom_left->has_ll &&
      children.child_bottom_right->has_ll) {
    return aifocore::Status::OkStatus();
  }

  const aifocore::Status st = isyntax::tile::LoadTile(
      isyntax, &isyntax->images[isyntax->wsi_image_index], tile->tile_scale,
      tile->tile_x, tile->tile_y, cache->ll_coeff_block_allocator,
      /*out_buffer_or_null=*/NULL,
      /*pixel_format=*/(isyntax_pixel_format_t)0);
  AIFOCORE_RETURN_IF_ERROR(st);
  return aifocore::Status::OkStatus();
}

static void MakeTileListsAddParentToList(isyntax_t* isyntax,
                                         isyntax_tile_t* tile,
                                         isyntax_tile_list_t* idwt_list,
                                         isyntax_tile_list_t* cache_list) {
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  const int parent_tile_scale = tile->tile_scale + 1;
  if (parent_tile_scale > wsi->max_scale) {
    return;
  }

  const int parent_tile_x = tile->tile_x / 2;
  const int parent_tile_y = tile->tile_y / 2;
  isyntax_level_t* parent_level = &wsi->levels[parent_tile_scale];
  isyntax_tile_t* parent_tile =
      &parent_level->tiles[parent_level->width_in_tiles * parent_tile_y +
                           parent_tile_x];
  if (parent_tile->exists && !parent_tile->cache_marked) {
    isyntax::tilelist::Remove(cache_list, parent_tile);
    parent_tile->cache_marked = true;
    TileListInsertFirst(idwt_list, parent_tile);
  }
}

static void MakeTileListsAddChildrenToList(isyntax_t* isyntax,
                                           isyntax_tile_t* tile,
                                           isyntax_tile_list_t* children_list,
                                           isyntax_tile_list_t* cache_list) {
  if (tile->tile_scale > 0) {
    isyntax_tile_children_t children = OpenslideComputeChildren(isyntax, tile);
    for (int i = 0; i < 4; ++i) {
      if (!children.as_array[i]->cache_marked) {
        isyntax::tilelist::Remove(cache_list, children.as_array[i]);
        TileListInsertFirst(children_list, children.as_array[i]);
      }
    }
  }
}

static void MakeTileListsByScale(isyntax_t* isyntax, int start_scale,
                                 isyntax_tile_list_t* idwt_list,
                                 isyntax_tile_list_t* coeff_list,
                                 isyntax_tile_list_t* children_list,
                                 isyntax_tile_list_t* cache_list) {
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  for (int scale = start_scale; scale <= wsi->max_scale; ++scale) {
    // Mark all neighbors of IDWT tiles at this level as requiring coefficients.
    isyntax_level_t* level = &wsi->levels[scale];
    for (ITERATE_TILE_LIST(tile, (*idwt_list))) {
      if (tile->tile_scale != scale) {
        continue;
      }
      for (int y_offset = -1; y_offset <= 1; ++y_offset) {
        for (int x_offset = -1; x_offset <= 1; ++x_offset) {
          const int neighbor_tile_x = tile->tile_x + x_offset;
          const int neighbor_tile_y = tile->tile_y + y_offset;
          if (neighbor_tile_x < 0 || neighbor_tile_x >= level->width_in_tiles ||
              neighbor_tile_y < 0 ||
              neighbor_tile_y >= level->height_in_tiles) {
            continue;
          }

          isyntax_tile_t* neighbor_tile =
              &level->tiles[level->width_in_tiles * neighbor_tile_y +
                            neighbor_tile_x];
          if (neighbor_tile->cache_marked || !neighbor_tile->exists) {
            continue;
          }

          isyntax::tilelist::Remove(cache_list, neighbor_tile);
          neighbor_tile->cache_marked = true;
          TileListInsertFirst(coeff_list, neighbor_tile);
        }
      }
    }

    // Mark all parents of tiles at this level as requiring IDWT so all tiles at
    // this level get their LL coefficients.
    for (ITERATE_TILE_LIST(tile, (*idwt_list))) {
      if (tile->tile_scale == scale) {
        MakeTileListsAddParentToList(isyntax, tile, idwt_list, cache_list);
      }
    }
    for (ITERATE_TILE_LIST(tile, (*coeff_list))) {
      if (tile->tile_scale == scale) {
        MakeTileListsAddParentToList(isyntax, tile, idwt_list, cache_list);
      }
    }
  }

  // Add all children of IDWT tiles that were not yet handled. Children will
  // have their LL coefficients written and should be cache-bumped.
  for (ITERATE_TILE_LIST(tile, (*idwt_list))) {
    MakeTileListsAddChildrenToList(isyntax, tile, children_list, cache_list);
  }
}

}  // namespace

namespace isyntax {

aifocore::Status TileRead(isyntax_t* isyntax, isyntax_cache_t* cache, int scale,
                          int tile_x, int tile_y, uint32_t* pixels_buffer,
                          enum isyntax_pixel_format_t pixel_format) {
  if (isyntax == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "TileRead: null isyntax");
  }
  if (cache == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "TileRead: null cache");
  }
  if (pixels_buffer == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "TileRead: null pixels_buffer");
  }
  if (cache->ll_coeff_block_allocator == nullptr ||
      cache->h_coeff_block_allocator == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "TileRead: cache allocators not initialized");
  }

  std::lock_guard<std::mutex> lock(cache->mutex);

  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  if (scale < 0 || scale >= wsi->level_count) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "TileRead: invalid scale");
  }
  isyntax_level_t* level = &wsi->levels[scale];

  if (!(tile_x >= 0 && tile_x < level->width_in_tiles && tile_y >= 0 &&
        tile_y < level->height_in_tiles)) {
    FillWhiteTile(isyntax, pixels_buffer);
    return aifocore::Status::OkStatus();
  }

  isyntax_tile_t* tile = &level->tiles[level->width_in_tiles * tile_y + tile_x];
  if (!tile->exists) {
    FillWhiteTile(isyntax, pixels_buffer);
    return aifocore::Status::OkStatus();
  }

  isyntax_tile_list_t idwt_list;
  isyntax_tile_list_t coeff_list;
  isyntax_tile_list_t children_list;
  isyntax::tilelist::Init(&idwt_list, "idwt_list");
  isyntax::tilelist::Init(&coeff_list, "coeff_list");
  isyntax::tilelist::Init(&children_list, "children_list");

  // Seed IDWT list with requested tile.
  isyntax::tilelist::Remove(&cache->cache_list, tile);
  tile->cache_marked = true;
  TileListInsertFirst(&idwt_list, tile);

  MakeTileListsByScale(isyntax, scale, &idwt_list, &coeff_list, &children_list,
                       &cache->cache_list);

  // Unmark visit status (lists keep ownership of ordering only).
  for (ITERATE_TILE_LIST(t, idwt_list)) {
    t->cache_marked = false;
  }
  for (ITERATE_TILE_LIST(t, coeff_list)) {
    t->cache_marked = false;
  }
  for (ITERATE_TILE_LIST(t, children_list)) {
    t->cache_marked = false;
  }

  // IO+decode: load coefficients where missing, then IDWT as needed
  // (top->down).
  for (ITERATE_TILE_LIST(t, coeff_list)) {
    AIFOCORE_RETURN_IF_ERROR(OpenslideLoadTileCoefficients(cache, isyntax, t));
  }
  for (ITERATE_TILE_LIST(t, idwt_list)) {
    AIFOCORE_RETURN_IF_ERROR(OpenslideLoadTileCoefficients(cache, isyntax, t));
  }
  for (ITERATE_TILE_LIST(t, idwt_list)) {
    if (t == idwt_list.tail) {
      AIFOCORE_RETURN_IF_ERROR(
          OpenslideIdwt(cache, isyntax, t, pixels_buffer, pixel_format));
    } else {
      AIFOCORE_RETURN_IF_ERROR(
          OpenslideIdwt(cache, isyntax, t, /*pixels_buffer=*/NULL,
                        /*pixel_format=*/(isyntax_pixel_format_t)0));
    }
  }

  // Cache bump.
  TileListInsertListFirst(&cache->cache_list, &children_list);
  TileListInsertListFirst(&cache->cache_list, &coeff_list);
  TileListInsertListFirst(&cache->cache_list, &idwt_list);

  // Cache trim.
  while (cache->cache_list.count > cache->target_cache_size) {
    isyntax_tile_t* victim = cache->cache_list.tail;
    isyntax::tilelist::Remove(&cache->cache_list, victim);
    for (int i = 0; i < 3; ++i) {
      if (victim->has_ll) {
        block_free(cache->ll_coeff_block_allocator,
                   victim->color_channels[i].coeff_ll);
        victim->color_channels[i].coeff_ll = NULL;
      }
      if (victim->has_h) {
        block_free(cache->h_coeff_block_allocator,
                   victim->color_channels[i].coeff_h);
        victim->color_channels[i].coeff_h = NULL;
      }
    }
    victim->has_ll = false;
    victim->has_h = false;
  }

  return aifocore::Status::OkStatus();
}

}  // namespace isyntax
