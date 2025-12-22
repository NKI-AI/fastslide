/*
  BSD 2-Clause License

  Copyright (c) 2019-2024, Pieter Valkema

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

#include "readers/isyntax/third_party/utils/block_allocator.h"

#include <cstdlib>
#include <mutex>
#include <new>

namespace isyntax::alloc {
namespace {

int32_t MaxCapacityBlocks(const block_allocator_t& allocator) {
  if (allocator.chunk_count <= 0 || allocator.chunk_capacity_in_blocks <= 0) {
    return 0;
  }
  return allocator.chunk_count * allocator.chunk_capacity_in_blocks;
}

}  // namespace

aifocore::Result<void*> BlockAlloc(block_allocator_t* allocator) {
  if (allocator == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "BlockAlloc: null allocator");
  }
  if (!allocator->is_valid) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "BlockAlloc: allocator not valid");
  }

  void* result = nullptr;
  std::lock_guard<std::mutex> lock(allocator->lock);
  if (allocator->free_list != nullptr) {
    // Grab a block from the free list.
    block_allocator_item_t* free_item = allocator->free_list;
    result = allocator->chunks[free_item->chunk_index].memory +
             free_item->block_index * allocator->block_size;
    allocator->free_list = free_item->next;
    --allocator->free_list_length;
  } else {
    if (allocator->used_chunks < 1) {
      return aifocore::Status(aifocore::StatusCode::kInternal,
                              "BlockAlloc: allocator has no chunks");
    }

    int32_t chunk_index = allocator->used_chunks - 1;
    block_allocator_chunk_t* current_chunk = allocator->chunks + chunk_index;
    if (current_chunk->used_blocks < allocator->chunk_capacity_in_blocks) {
      int32_t block_index = static_cast<int32_t>(current_chunk->used_blocks++);
      result = current_chunk->memory + block_index * allocator->block_size;
    } else {
      // Chunk is full, allocate a new chunk.
      if (allocator->used_chunks < allocator->chunk_count) {
        chunk_index = allocator->used_chunks++;
        current_chunk = allocator->chunks + chunk_index;
        if (current_chunk->memory != nullptr) {
          return aifocore::Status(
              aifocore::StatusCode::kInternal,
              "BlockAlloc: unexpected non-null chunk memory");
        }
        current_chunk->memory =
            static_cast<uint8_t*>(std::malloc(allocator->chunk_size));
        if (current_chunk->memory == nullptr) {
          return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                                  "BlockAlloc: failed to allocate new chunk");
        }
        int32_t block_index =
            static_cast<int32_t>(current_chunk->used_blocks++);
        result = current_chunk->memory + block_index * allocator->block_size;
      } else {
        return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                                "BlockAlloc: allocator out of capacity");
      }
    }
  }

  if (result == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "BlockAlloc: returned null block");
  }
  return result;
}

aifocore::Status BlockFree(block_allocator_t* allocator, void* ptr_to_free) {
  if (allocator == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "BlockFree: null allocator");
  }
  if (!allocator->is_valid) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "BlockFree: allocator not valid");
  }
  if (ptr_to_free == nullptr) {
    return aifocore::Status::OkStatus();
  }

  std::lock_guard<std::mutex> lock(allocator->lock);
  // Find the right chunk.
  int32_t chunk_index = -1;
  for (int32_t i = 0; i < allocator->used_chunks; ++i) {
    block_allocator_chunk_t* chunk = allocator->chunks + i;
    bool match = (static_cast<uint8_t*>(ptr_to_free) >= chunk->memory &&
                  static_cast<uint8_t*>(ptr_to_free) <
                      (chunk->memory + allocator->chunk_size));
    if (match) {
      chunk_index = i;
      break;
    }
  }
  if (chunk_index < 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "BlockFree: pointer not owned by allocator");
  }

  const int32_t max_capacity_blocks = MaxCapacityBlocks(*allocator);
  if (max_capacity_blocks <= 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "BlockFree: invalid allocator capacity");
  }
  if (allocator->free_list_length >= max_capacity_blocks) {
    return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                            "BlockFree: free list overflow");
  }

  block_allocator_chunk_t* chunk = allocator->chunks + chunk_index;
  block_allocator_item_t free_item = {};
  free_item.next = allocator->free_list;
  free_item.chunk_index = chunk_index;
  free_item.block_index = static_cast<int32_t>(
      (static_cast<uint8_t*>(ptr_to_free) - chunk->memory) /
      allocator->block_size);
  int32_t free_index = allocator->free_list_length++;
  allocator->free_list_storage[free_index] = free_item;
  allocator->free_list = allocator->free_list_storage + free_index;
  return aifocore::Status::OkStatus();
}

}  // namespace isyntax::alloc

block_allocator_t* block_allocator_create(size_t block_size,
                                          size_t max_capacity_in_blocks,
                                          size_t chunk_size) {
  void* raw = std::malloc(sizeof(block_allocator_t));
  if (raw == nullptr) {
    return nullptr;
  }
  auto* allocator = new (raw) block_allocator_t();

  uint64_t total_capacity = static_cast<uint64_t>(block_size) *
                            static_cast<uint64_t>(max_capacity_in_blocks);
  uint64_t chunk_count = total_capacity / chunk_size;
  uint64_t chunk_capacity_in_blocks = max_capacity_in_blocks / chunk_count;
  allocator->block_size = block_size;
  allocator->chunk_capacity_in_blocks =
      static_cast<int32_t>(chunk_capacity_in_blocks);
  allocator->chunk_size = chunk_size;
  ASSERT(chunk_count > 0);
  allocator->chunk_count = static_cast<int32_t>(chunk_count);
  allocator->used_chunks = 1;
  allocator->chunks = static_cast<block_allocator_chunk_t*>(std::calloc(
      1, static_cast<size_t>(chunk_count) * sizeof(block_allocator_chunk_t)));
  if (allocator->chunks == nullptr) {
    allocator->~block_allocator_t();
    std::free(allocator);
    return nullptr;
  }
  allocator->chunks[0].memory = static_cast<uint8_t*>(std::malloc(chunk_size));
  if (allocator->chunks[0].memory == nullptr) {
    std::free(allocator->chunks);
    allocator->~block_allocator_t();
    std::free(allocator);
    return nullptr;
  }
  allocator->free_list_storage = static_cast<block_allocator_item_t*>(
      std::calloc(1, max_capacity_in_blocks * sizeof(block_allocator_item_t)));
  if (allocator->free_list_storage == nullptr) {
    std::free(allocator->chunks[0].memory);
    std::free(allocator->chunks);
    allocator->~block_allocator_t();
    std::free(allocator);
    return nullptr;
  }
  allocator->is_valid = true;
  return allocator;
}

void block_allocator_destroy(block_allocator_t* allocator) {
  if (allocator == nullptr) {
    return;
  }
  for (int32_t i = 0; i < allocator->used_chunks; ++i) {
    block_allocator_chunk_t* chunk = allocator->chunks + i;
    if (chunk->memory) {
      std::free(chunk->memory);
    }
  }
  if (allocator->chunks) {
    std::free(allocator->chunks);
  }
  if (allocator->free_list_storage) {
    std::free(allocator->free_list_storage);
  }
  allocator->~block_allocator_t();
  std::free(allocator);
}

void* block_alloc(block_allocator_t* allocator) {
  auto res = isyntax::alloc::BlockAlloc(allocator);
  if (!res.ok()) {
    return nullptr;
  }
  return res.value();
}

void block_free(block_allocator_t* allocator, void* ptr_to_free) {
  (void)isyntax::alloc::BlockFree(allocator, ptr_to_free);
}
