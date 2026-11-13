//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice,
//  this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE

#include "fastslide/readers/isyntax/third_party/cache.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/reader.h"
#include "fastslide/readers/isyntax/third_party/utils/block_allocator.h"

namespace isyntax {
namespace {

template <typename T>
using MallocPtr = std::unique_ptr<T, decltype(&std::free)>;

class BlockAllocatorOwner {
 public:
  BlockAllocatorOwner() = default;

  explicit BlockAllocatorOwner(block_allocator_t* allocator)
      : allocator_(allocator) {}

  BlockAllocatorOwner(const BlockAllocatorOwner&) = delete;
  BlockAllocatorOwner& operator=(const BlockAllocatorOwner&) = delete;

  BlockAllocatorOwner(BlockAllocatorOwner&& other) noexcept
      : allocator_(other.allocator_) {
    other.allocator_ = nullptr;
  }

  BlockAllocatorOwner& operator=(BlockAllocatorOwner&& other) noexcept {
    if (this != &other) {
      Reset();
      allocator_ = other.allocator_;
      other.allocator_ = nullptr;
    }
    return *this;
  }

  ~BlockAllocatorOwner() { Reset(); }

  block_allocator_t* get() const { return allocator_; }

  block_allocator_t* release() {
    block_allocator_t* out = allocator_;
    allocator_ = nullptr;
    return out;
  }

 private:
  void Reset() {
    if (allocator_ == nullptr) {
      return;
    }
    // `block_allocator_destroy` frees the allocator struct too.
    if (allocator_->is_valid) {
      block_allocator_destroy(allocator_);
    } else {
      std::free(allocator_);
    }
    allocator_ = nullptr;
  }

  block_allocator_t* allocator_ = nullptr;
};

aifocore::Status CacheError(std::string_view message) {
  return aifocore::Status(aifocore::StatusCode::kInternal,
                          std::string(message));
}

void DestroyCacheHandle(isyntax_cache_t* handle) {
  if (handle == nullptr) {
    return;
  }
  handle->~isyntax_cache_t();
  std::free(handle);
}

}  // namespace

aifocore::Result<std::unique_ptr<IsyntaxCache>> IsyntaxCache::CreateAndInject(
    std::string_view debug_name_or_null, size_t capacity_bytes,
    isyntax_t* isyntax) {
  if (isyntax == nullptr) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        "IsyntaxCache::CreateAndInject called with null isyntax");
  }
  if (capacity_bytes == 0) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        "IsyntaxCache::CreateAndInject called with zero capacity_bytes");
  }

  // The cache owns its allocators. Injection is only valid for an isyntax
  // object which did not initialize allocators itself.
  if (isyntax->ll_coeff_block_allocator != nullptr ||
      isyntax->h_coeff_block_allocator != nullptr) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        "IsyntaxCache::CreateAndInject: isyntax already has allocators");
  }

  void* raw_cache = std::malloc(sizeof(isyntax_cache_t));
  if (raw_cache == nullptr) {
    return CacheError("Failed to allocate isyntax_cache_t");
  }
  std::unique_ptr<isyntax_cache_t, decltype(&DestroyCacheHandle)> cache_ptr(
      new (raw_cache) isyntax_cache_t(), &DestroyCacheHandle);

  std::string debug_name(debug_name_or_null);
  isyntax::tilelist::Init(&cache_ptr->cache_list,
                          debug_name.empty() ? nullptr : debug_name.c_str());
  cache_ptr->capacity_bytes = capacity_bytes;
  cache_ptr->allocator_block_width = isyntax->block_width;
  cache_ptr->allocator_block_height = isyntax->block_height;

  const size_t ll_coeff_block_size =
      static_cast<size_t>(isyntax->block_width) *
      static_cast<size_t>(isyntax->block_height) * sizeof(icoeff_t);
  if (ll_coeff_block_size == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid block size for cache allocator sizing");
  }

  const size_t max_capacity_blocks = GIGABYTES(32) / ll_coeff_block_size;
  const size_t ll_capacity_blocks = max_capacity_blocks / 4;
  const size_t h_coeff_block_size = ll_coeff_block_size * 3;
  const size_t h_capacity_blocks = ll_capacity_blocks * 3;

  block_allocator_t* ll_alloc = block_allocator_create(
      ll_coeff_block_size, ll_capacity_blocks, MEGABYTES(256));
  if (ll_alloc == nullptr) {
    return CacheError("Failed to create LL coeff block allocator");
  }
  BlockAllocatorOwner ll_owner(ll_alloc);

  block_allocator_t* h_alloc = block_allocator_create(
      h_coeff_block_size, h_capacity_blocks, MEGABYTES(256));
  if (h_alloc == nullptr) {
    return CacheError("Failed to create H coeff block allocator");
  }
  BlockAllocatorOwner h_owner(h_alloc);

  cache_ptr->ll_coeff_block_allocator = ll_owner.release();
  cache_ptr->h_coeff_block_allocator = h_owner.release();
  cache_ptr->is_block_allocator_owned = true;

  // Inject pointers into isyntax for compatibility with existing code paths.
  isyntax->ll_coeff_block_allocator = cache_ptr->ll_coeff_block_allocator;
  isyntax->h_coeff_block_allocator = cache_ptr->h_coeff_block_allocator;
  isyntax->is_block_allocator_owned = false;

  // `IsyntaxCache` takes ownership of the raw handle.
  return std::unique_ptr<IsyntaxCache>(new IsyntaxCache(cache_ptr.release()));
}

namespace {

std::mutex g_shared_mutex;
size_t g_shared_capacity_bytes = IsyntaxCache::kDefaultSharedCapacityBytes;
// Shared caches by block width and height; a cache lives while a file uses it.
std::map<std::pair<int32_t, int32_t>, std::weak_ptr<IsyntaxCache>>
    g_shared_caches;

}  // namespace

aifocore::Result<std::shared_ptr<IsyntaxCache>> IsyntaxCache::Shared(
    isyntax_t* isyntax) {
  if (isyntax == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "IsyntaxCache::Shared called with null isyntax");
  }
  if (isyntax->ll_coeff_block_allocator != nullptr ||
      isyntax->h_coeff_block_allocator != nullptr) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        "IsyntaxCache::Shared: isyntax already has allocators");
  }
  std::lock_guard<std::mutex> lock(g_shared_mutex);
  auto& entry = g_shared_caches[{isyntax->block_width, isyntax->block_height}];
  std::shared_ptr<IsyntaxCache> cache = entry.lock();
  if (cache == nullptr) {
    AIFOCORE_ASSIGN_OR_RETURN(
        std::unique_ptr<IsyntaxCache> created,
        CreateAndInject("IsyntaxSharedCache", g_shared_capacity_bytes,
                        isyntax));
    cache = std::move(created);
    entry = cache;
    return cache;
  }
  isyntax->ll_coeff_block_allocator = cache->handle_->ll_coeff_block_allocator;
  isyntax->h_coeff_block_allocator = cache->handle_->h_coeff_block_allocator;
  isyntax->is_block_allocator_owned = false;
  return cache;
}

void IsyntaxCache::SetSharedCapacityBytes(size_t capacity_bytes) {
  std::lock_guard<std::mutex> lock(g_shared_mutex);
  g_shared_capacity_bytes = capacity_bytes;
  for (auto& [block_size, weak_cache] : g_shared_caches) {
    if (std::shared_ptr<IsyntaxCache> cache = weak_cache.lock()) {
      std::lock_guard<std::mutex> cache_lock(cache->handle_->mutex);
      cache->handle_->capacity_bytes = capacity_bytes;
    }
  }
}

size_t IsyntaxCache::SharedCapacityBytes() {
  std::lock_guard<std::mutex> lock(g_shared_mutex);
  return g_shared_capacity_bytes;
}

void IsyntaxCache::Release(isyntax_t* isyntax) {
  if (isyntax == nullptr || handle_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(handle_->mutex);
  isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
  for (int32_t level = 0; level < wsi->level_count; ++level) {
    isyntax_level_t* l = &wsi->levels[level];
    if (l->tiles == nullptr) {
      continue;
    }
    for (int64_t i = 0; i < static_cast<int64_t>(l->tile_count); ++i) {
      isyntax_tile_t* tile = &l->tiles[i];
      isyntax::tilelist::Remove(&handle_->cache_list, tile);
      for (isyntax_tile_channel_t& channel : tile->color_channels) {
        if (channel.coeff_ll != nullptr) {
          block_free(handle_->ll_coeff_block_allocator, channel.coeff_ll);
          channel.coeff_ll = nullptr;
        }
        if (channel.coeff_h != nullptr) {
          block_free(handle_->h_coeff_block_allocator, channel.coeff_h);
          channel.coeff_h = nullptr;
        }
      }
      tile->has_ll = false;
      tile->has_h = false;
    }
  }
}

IsyntaxCache::~IsyntaxCache() {
  if (handle_ == nullptr) {
    return;
  }
  if (handle_->is_block_allocator_owned) {
    if (handle_->ll_coeff_block_allocator != nullptr) {
      block_allocator_destroy(handle_->ll_coeff_block_allocator);
      handle_->ll_coeff_block_allocator = nullptr;
    }
    if (handle_->h_coeff_block_allocator != nullptr) {
      block_allocator_destroy(handle_->h_coeff_block_allocator);
      handle_->h_coeff_block_allocator = nullptr;
    }
  }

  DestroyCacheHandle(handle_);
  handle_ = nullptr;
}

}  // namespace isyntax
