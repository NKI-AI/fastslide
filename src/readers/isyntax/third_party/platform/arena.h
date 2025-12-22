/*
  BSD 2-Clause License

  Copyright (c) 2019-2023, Pieter Valkema

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

#include "readers/isyntax/third_party/platform/common.h"

// `std::uintptr_t` lives in <cstdint> (C++), not <stdint.h> (C).
#include <cstdint>

typedef struct arena_t {
  size_t size;
  uint8_t* base;
  size_t used;
  int32_t temp_count;
} arena_t;

typedef struct temp_memory_t {
  arena_t* arena;
  size_t used;
  int32_t temp_index;
} temp_memory_t;

static inline void init_arena(arena_t* arena, size_t size, void* base) {
  arena_t new_arena = {
      .size = size,
      .base = reinterpret_cast<uint8_t*>(base),
      .used = 0,
      .temp_count = 0,
  };
  *arena = new_arena;
}

static inline void* arena_current_pos(arena_t* arena) {
  return arena->base + arena->used;
}

#define arena_push_struct(arena, type) \
  (reinterpret_cast<type*>(arena_push_size((arena), sizeof(type))))
#define arena_push_array(arena, count, type) \
  (reinterpret_cast<type*>(arena_push_size((arena), (count) * sizeof(type))))

static inline void* arena_push_size(arena_t* arena, size_t size) {
  ASSERT((arena->used + size) <= arena->size);
  void* result = arena->base + arena->used;
  arena->used += size;
  return result;
}

static inline void arena_align(arena_t* arena, uint32_t alignment) {
  ASSERT(alignment > 0);
  std::uintptr_t pos =
      reinterpret_cast<std::uintptr_t>(arena->base) + arena->used;
  std::uintptr_t new_pos = ((pos + alignment - 1) / alignment) * alignment;
  arena->used += static_cast<size_t>(new_pos - pos);
}

static inline temp_memory_t begin_temp_memory(arena_t* arena) {
  temp_memory_t result = {
      .arena = arena,
      .used = arena->used,
      .temp_index = arena->temp_count,
  };
  ++arena->temp_count;
  return result;
}

static inline void release_temp_memory(temp_memory_t* temp) {
  ASSERT(temp->arena->temp_count > 0);
  temp->arena->temp_count--;
  temp->arena->used = temp->used;
  ASSERT(temp->temp_index == temp->arena->temp_count);
}

static inline int64_t arena_get_bytes_left(arena_t* arena) {
  return arena->size - arena->used;
}
