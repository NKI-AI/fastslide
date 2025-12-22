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
#pragma once

#include "readers/isyntax/third_party/isyntax.h"
#include "readers/isyntax/third_party/isyntax_types.h"

#ifdef __cplusplus
#include <mutex>
#include "aifocore/status/result.h"

struct isyntax_tile_list_t {
  isyntax_tile_t* head = nullptr;
  isyntax_tile_t* tail = nullptr;
  int count = 0;
  const char* dbg_name = nullptr;
};

struct isyntax_cache_t {
  isyntax_tile_list_t cache_list{};
  std::mutex mutex{};
  // TODO(avirodov): int refcount;
  int target_cache_size = 0;
  block_allocator_t* ll_coeff_block_allocator = nullptr;
  block_allocator_t* h_coeff_block_allocator = nullptr;
  bool is_block_allocator_owned = false;
  int allocator_block_width = 0;
  int allocator_block_height = 0;
};

namespace isyntax {

namespace tilelist {

// Intrusive doubly-linked list over `isyntax_tile_t` using
// `cache_next/cache_prev`. This is used as an LRU for coefficient caching; it
// intentionally does not allocate per-node like `std::list`.
inline void Init(isyntax_tile_list_t* list, const char* dbg_name) {
  list->head = nullptr;
  list->tail = nullptr;
  list->count = 0;
  list->dbg_name = dbg_name;
}

inline void Remove(isyntax_tile_list_t* list, isyntax_tile_t* tile) {
  if (!tile->cache_next && !tile->cache_prev && !(list->head == tile) &&
      !(list->tail == tile)) {
    // Not part of any list.
    return;
  }
  if (list->head == tile) {
    list->head = tile->cache_next;
  }
  if (list->tail == tile) {
    list->tail = tile->cache_prev;
  }
  if (tile->cache_prev) {
    tile->cache_prev->cache_next = tile->cache_next;
  }
  if (tile->cache_next) {
    tile->cache_next->cache_prev = tile->cache_prev;
  }
  tile->cache_next = nullptr;
  tile->cache_prev = nullptr;
  list->count--;
}

}  // namespace tilelist

// C++ tile read entry point: returns Status instead of printing/logging.
aifocore::Status TileRead(isyntax_t* isyntax, isyntax_cache_t* cache, int scale,
                          int tile_x, int tile_y, uint32_t* pixels_buffer,
                          enum isyntax_pixel_format_t pixel_format);

}  // namespace isyntax
#endif
