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
#include <span>
#include <vector>

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

// Codeblocks a read decodes for one tile, out of the cache lock.
struct CoeffLoad {
  isyntax_tile_t* tile = nullptr;
  bool ll = false;
  bool h = false;
  icoeff_t* coeff_ll[3] = {};
  icoeff_t* coeff_h[3] = {};
};

bool TileNeedsLlCodeblocks(const isyntax_image_t* wsi,
                           const isyntax_tile_t* tile) {
  // Only top-level tiles read their LL coefficients from the file. For other
  // levels, LL comes from parent IDWT.
  return tile->exists && tile->tile_scale == wsi->max_scale && !tile->has_ll;
}

bool TileNeedsHCodeblocks(const isyntax_tile_t* tile) {
  return tile->exists && !tile->has_h;
}

aifocore::Status DecodeCodeblocks(isyntax_cache_t* cache, isyntax_t* isyntax,
                                  const isyntax_tile_t* tile,
                                  int codeblock_index, bool is_ll,
                                  icoeff_t* out_coeff[3]) {
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  isyntax_data_chunk_t* chunk = &wsi->data_chunks[tile->data_chunk_index];
  block_allocator_t* allocator =
      is_ll ? cache->ll_coeff_block_allocator : cache->h_coeff_block_allocator;

  for (int color = 0; color < 3; ++color) {
    isyntax_codeblock_t* codeblock =
        &wsi->codeblocks[codeblock_index +
                         color * chunk->codeblock_count_per_color];
    ASSERT(codeblock->coefficient == (is_ll ? 0 : 1));
    ASSERT(codeblock->color_component == (uint32_t)color);
    ASSERT(codeblock->scale == (uint32_t)tile->tile_scale);

    auto coeff_or = isyntax::alloc::BlockAlloc(allocator);
    if (!coeff_or.ok()) {
      return coeff_or.status();
    }
    out_coeff[color] = static_cast<icoeff_t*>(*coeff_or);

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

    const aifocore::Status st = isyntax::HulskenDecompress(
        std::span<uint8_t>(reinterpret_cast<uint8_t*>(codeblock_data.get()),
                           static_cast<size_t>(codeblock->block_size)),
        isyntax->block_width, isyntax->block_height, codeblock->coefficient,
        wsi->compressor_version,
        std::span<int16_t>(reinterpret_cast<int16_t*>(out_coeff[color]),
                           expected_coeff_count));
    AIFOCORE_RETURN_IF_ERROR(st);
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status DecodeTileCoefficients(isyntax_cache_t* cache,
                                        isyntax_t* isyntax, CoeffLoad& load) {
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  const isyntax_tile_t* tile = load.tile;

  if (load.ll) {
    AIFOCORE_RETURN_IF_ERROR(
        DecodeCodeblocks(cache, isyntax, tile,
                         /*codeblock_index=*/tile->codeblock_index,
                         /*is_ll=*/true, load.coeff_ll));
  }

  if (load.h) {
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

    AIFOCORE_RETURN_IF_ERROR(
        DecodeCodeblocks(cache, isyntax, tile,
                         /*codeblock_index=*/tile->codeblock_chunk_index +
                             codeblock_index_in_chunk,
                         /*is_ll=*/false, load.coeff_h));
  }

  return aifocore::Status::OkStatus();
}

/// Decodes the codeblocks the tiles miss. Called with `lock` held, which it
/// releases while decoding. Reads claim one tile at a time, so that reads
/// needing the same tiles decode them in parallel; tiles another read is
/// decoding are waited for.
aifocore::Status LoadTileCoefficients(isyntax_cache_t* cache,
                                      isyntax_t* isyntax,
                                      std::span<isyntax_tile_t* const> tiles,
                                      std::unique_lock<std::mutex>& lock) {
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  for (;;) {
    CoeffLoad load;
    bool is_waiting = false;
    for (isyntax_tile_t* tile : tiles) {
      const bool ll = TileNeedsLlCodeblocks(wsi, tile);
      const bool h = TileNeedsHCodeblocks(tile);
      if (!ll && !h) {
        continue;
      }
      if (tile->is_coeff_loading) {
        is_waiting = true;
        continue;
      }
      tile->is_coeff_loading = true;
      load = CoeffLoad{.tile = tile, .ll = ll, .h = h};
      break;
    }

    if (load.tile != nullptr) {
      lock.unlock();
      const aifocore::Status st = DecodeTileCoefficients(cache, isyntax, load);
      lock.lock();
      for (int color = 0; color < 3; ++color) {
        if (!st.ok()) {
          if (load.coeff_ll[color] != nullptr) {
            block_free(cache->ll_coeff_block_allocator, load.coeff_ll[color]);
          }
          if (load.coeff_h[color] != nullptr) {
            block_free(cache->h_coeff_block_allocator, load.coeff_h[color]);
          }
          continue;
        }
        if (load.ll) {
          load.tile->color_channels[color].coeff_ll = load.coeff_ll[color];
        }
        if (load.h) {
          load.tile->color_channels[color].coeff_h = load.coeff_h[color];
        }
      }
      if (st.ok() && load.ll) {
        load.tile->has_ll = true;
      }
      if (st.ok() && load.h) {
        load.tile->has_h = true;
      }
      load.tile->is_coeff_loading = false;
      cache->tile_ready.notify_all();
      AIFOCORE_RETURN_IF_ERROR(st);
    } else if (is_waiting) {
      cache->tile_ready.wait(lock);
    } else {
      return aifocore::Status::OkStatus();
    }
  }
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

bool ChildrenHaveLl(isyntax_t* isyntax, isyntax_tile_t* tile) {
  if (tile->tile_scale == 0) {
    return true;
  }
  const isyntax_tile_children_t children =
      OpenslideComputeChildren(isyntax, tile);
  return children.child_top_left->has_ll && children.child_top_right->has_ll &&
         children.child_bottom_left->has_ll &&
         children.child_bottom_right->has_ll;
}

/// Inverse wavelet transform of a tile: writes the LL coefficients its
/// children miss, and its pixels if asked. Called with `lock` held, which it
/// releases while transforming. Only one read at a time writes the LL
/// coefficients of the children of a tile; the others wait for it.
aifocore::Status OpenslideIdwt(isyntax_cache_t* cache, isyntax_t* isyntax,
                               isyntax_tile_t* tile, uint32_t* pixels_buffer,
                               enum isyntax_pixel_format_t pixel_format,
                               std::unique_lock<std::mutex>& lock) {
  cache->tile_ready.wait(lock, [tile] { return !tile->is_idwt_running; });

  // If all children have ll coefficients and we don't need rgb pixels, we can
  // skip IDWT.
  const bool writes_children = !ChildrenHaveLl(isyntax, tile);
  if (!writes_children && pixels_buffer == nullptr) {
    return aifocore::Status::OkStatus();
  }

  tile->is_idwt_running = writes_children;
  lock.unlock();
  const aifocore::Status st = isyntax::tile::LoadTile(
      isyntax, &isyntax->images[isyntax->wsi_image_index], tile->tile_scale,
      tile->tile_x, tile->tile_y, cache->ll_coeff_block_allocator,
      pixels_buffer, pixel_format);
  lock.lock();
  if (writes_children) {
    if (st.ok()) {
      const isyntax_tile_children_t children =
          OpenslideComputeChildren(isyntax, tile);
      for (isyntax_tile_t* child : children.as_array) {
        child->has_ll = true;
      }
    }
    tile->is_idwt_running = false;
    cache->tile_ready.notify_all();
  }
  return st;
}

/// Tiles a read needs. Only the read that collected them uses it, unlike the
/// lists linked through the tiles.
using TileArray = std::vector<isyntax_tile_t*>;

static void MakeTileListsAddParentToList(isyntax_t* isyntax,
                                         isyntax_tile_t* tile,
                                         TileArray* idwt_list) {
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
    parent_tile->cache_marked = true;
    idwt_list->push_back(parent_tile);
  }
}

static void MakeTileListsAddChildrenToList(isyntax_t* isyntax,
                                           isyntax_tile_t* tile,
                                           TileArray* children_list) {
  if (tile->tile_scale > 0) {
    isyntax_tile_children_t children = OpenslideComputeChildren(isyntax, tile);
    for (isyntax_tile_t* child : children.as_array) {
      if (!child->cache_marked) {
        child->cache_marked = true;
        children_list->push_back(child);
      }
    }
  }
}

/// Lists are ordered children first: parents are at the end.
static void MakeTileListsByScale(isyntax_t* isyntax, int start_scale,
                                 TileArray* idwt_list, TileArray* coeff_list,
                                 TileArray* children_list) {
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  for (int scale = start_scale; scale <= wsi->max_scale; ++scale) {
    // Mark all neighbors of IDWT tiles at this level as requiring coefficients.
    isyntax_level_t* level = &wsi->levels[scale];
    for (size_t i = 0; i < idwt_list->size(); ++i) {
      isyntax_tile_t* tile = (*idwt_list)[i];
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

          neighbor_tile->cache_marked = true;
          coeff_list->push_back(neighbor_tile);
        }
      }
    }

    // Mark all parents of tiles at this level as requiring IDWT so all tiles at
    // this level get their LL coefficients.
    for (size_t i = 0; i < idwt_list->size(); ++i) {
      if ((*idwt_list)[i]->tile_scale == scale) {
        MakeTileListsAddParentToList(isyntax, (*idwt_list)[i], idwt_list);
      }
    }
    for (isyntax_tile_t* tile : *coeff_list) {
      if (tile->tile_scale == scale) {
        MakeTileListsAddParentToList(isyntax, tile, idwt_list);
      }
    }
  }

  // Add all children of IDWT tiles that were not yet handled. Children will
  // have their LL coefficients written and should be cache-bumped.
  for (isyntax_tile_t* tile : *idwt_list) {
    MakeTileListsAddChildrenToList(isyntax, tile, children_list);
  }
}

/// Takes the tiles out of the cache list while a read uses them, so that other
/// reads cannot evict them.
void PinTiles(isyntax_cache_t* cache, const TileArray& tiles) {
  for (isyntax_tile_t* tile : tiles) {
    tile->cache_marked = false;
    if (tile->cache_pin_count++ == 0) {
      isyntax::tilelist::Remove(&cache->cache_list, tile);
    }
  }
}

/// Puts the tiles no read uses anymore back first in the cache list, the last
/// ones of the array (parents) at the head.
void UnpinTiles(isyntax_cache_t* cache, const TileArray& tiles) {
  for (isyntax_tile_t* tile : tiles) {
    if (--tile->cache_pin_count == 0) {
      TileListInsertFirst(&cache->cache_list, tile);
    }
  }
}

/// Decodes and transforms the tiles a read pinned, and the pixels of `tile`.
aifocore::Status ReadPinnedTiles(isyntax_t* isyntax, isyntax_cache_t* cache,
                                 isyntax_tile_t* tile, uint32_t* pixels_buffer,
                                 enum isyntax_pixel_format_t pixel_format,
                                 const TileArray& idwt_list,
                                 const TileArray& coeff_list,
                                 std::unique_lock<std::mutex>& lock) {
  // IO+decode: load coefficients where missing (H, and LL for top tiles).
  TileArray coeff_tiles(coeff_list);
  coeff_tiles.insert(coeff_tiles.end(), idwt_list.begin(), idwt_list.end());
  AIFOCORE_RETURN_IF_ERROR(
      LoadTileCoefficients(cache, isyntax, coeff_tiles, lock));

  // IDWT as needed, top to bottom: parents give their children the LL
  // coefficients the neighbors at the next scale need. The requested tile,
  // first in the list, comes last and produces the pixels.
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  for (int scale = wsi->max_scale; scale > tile->tile_scale; --scale) {
    for (size_t i = 1; i < idwt_list.size(); ++i) {
      if (idwt_list[i]->tile_scale == scale) {
        AIFOCORE_RETURN_IF_ERROR(OpenslideIdwt(
            cache, isyntax, idwt_list[i], /*pixels_buffer=*/nullptr,
            /*pixel_format=*/(isyntax_pixel_format_t)0, lock));
      }
    }
  }
  return OpenslideIdwt(cache, isyntax, tile, pixels_buffer, pixel_format, lock);
}

}  // namespace

namespace isyntax {

void TrimCache(isyntax_cache_t* cache) {
  // Tiles other reads use are not in the list. Blocks are freed even without
  // has_ll / has_h: a failed transform leaves them unreported.
  const auto used_bytes = [cache] {
    return block_allocator_used_bytes(cache->ll_coeff_block_allocator) +
           block_allocator_used_bytes(cache->h_coeff_block_allocator);
  };
  while (cache->cache_list.tail != nullptr &&
         used_bytes() > cache->capacity_bytes) {
    isyntax_tile_t* victim = cache->cache_list.tail;
    isyntax::tilelist::Remove(&cache->cache_list, victim);
    for (int i = 0; i < 3; ++i) {
      if (victim->color_channels[i].coeff_ll != nullptr) {
        block_free(cache->ll_coeff_block_allocator,
                   victim->color_channels[i].coeff_ll);
        victim->color_channels[i].coeff_ll = nullptr;
      }
      if (victim->color_channels[i].coeff_h != nullptr) {
        block_free(cache->h_coeff_block_allocator,
                   victim->color_channels[i].coeff_h);
        victim->color_channels[i].coeff_h = nullptr;
      }
    }
    victim->has_ll = false;
    victim->has_h = false;
  }
}

aifocore::Status TileRead(isyntax_t* isyntax, isyntax_cache_t* cache, int scale,
                          int tile_x, int tile_y, uint32_t* pixels_buffer,
                          enum isyntax_pixel_format_t pixel_format) {
  // The cache lock guards the tiles' cache fields and the lists, and is
  // released while decoding and transforming: reads run in parallel. A read
  // pins the tiles it needs so that they are not evicted, and claims the work
  // it does on a tile (is_coeff_loading, is_idwt_running) so that other reads
  // needing it wait instead of redoing it.
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

  // Decoding uses memory local to the thread.
  if (!local_thread_memory) {
    init_thread_memory(0, &global_system_info);
  }

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

  // Need 3 lists:
  // 1. idwt list - those tiles will have to perform an idwt for their children
  //    to get ll coeffs. Primary cache bump.
  // 2. coeff list - those tiles are neighbors and will need to have
  //    coefficients loaded. Secondary cache bump.
  // 3. children list - those tiles will have their ll coeffs loaded as a side
  //    effect. Tertiary cache bump.
  // Those lists are disjoint.
  TileArray idwt_list;
  TileArray coeff_list;
  TileArray children_list;

  std::unique_lock<std::mutex> lock(cache->mutex);

  tile->cache_marked = true;
  idwt_list.push_back(tile);
  MakeTileListsByScale(isyntax, scale, &idwt_list, &coeff_list, &children_list);
  PinTiles(cache, idwt_list);
  PinTiles(cache, coeff_list);
  PinTiles(cache, children_list);

  const aifocore::Status st =
      ReadPinnedTiles(isyntax, cache, tile, pixels_buffer, pixel_format,
                      idwt_list, coeff_list, lock);

  // Cache bump.
  UnpinTiles(cache, children_list);
  UnpinTiles(cache, coeff_list);
  UnpinTiles(cache, idwt_list);

  TrimCache(cache);

  return st;
}

}  // namespace isyntax
