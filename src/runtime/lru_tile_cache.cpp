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

#include "fastslide/runtime/lru_tile_cache.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "aifocore/status/result.h"

namespace fastslide {
namespace runtime {

aifocore::Result<std::shared_ptr<LRUTileCache>> LRUTileCache::Create(
    size_t capacity_bytes) {
  if (capacity_bytes == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Cache capacity must be greater than 0");
  }
  return std::make_shared<LRUTileCache>(capacity_bytes);
}

LRUTileCache::LRUTileCache(size_t capacity_bytes)
    : capacity_bytes_(capacity_bytes),
      current_size_bytes_(0),
      cache_{},
      lru_list_{},
      hits_(0),
      misses_(0) {}

std::shared_ptr<CachedTileData> LRUTileCache::Get(const TileKey& key) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto iter = cache_.find(key);
  if (iter == cache_.end()) {
    ++misses_;
    return nullptr;
  }

  ++hits_;

  // Move to front of LRU list
  lru_list_.erase(iter->second.lru_it);
  lru_list_.push_front(key);
  iter->second.lru_it = lru_list_.begin();

  return iter->second.tile;
}

void LRUTileCache::Put(const TileKey& key,
                       std::shared_ptr<CachedTileData> tile) {
  if (!tile) {
    return;  // Don't cache null tiles
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // Check if already exists
  auto iter = cache_.find(key);
  if (iter != cache_.end()) {
    // Update existing entry
    size_t old_size = iter->second.tile->GetMemoryUsage();
    size_t new_size = tile->GetMemoryUsage();

    iter->second.tile = tile;
    current_size_bytes_ = current_size_bytes_ - old_size + new_size;

    // Move to front of LRU list
    lru_list_.erase(iter->second.lru_it);
    lru_list_.push_front(key);
    iter->second.lru_it = lru_list_.begin();

    // Evict if needed after update
    while (current_size_bytes_ > capacity_bytes_ && !lru_list_.empty()) {
      EvictLru();
    }
    return;
  }

  size_t tile_size = tile->GetMemoryUsage();

  // Don't cache if single tile is larger than capacity
  if (tile_size > capacity_bytes_) {
    return;
  }

  // Evict if needed before adding
  while (current_size_bytes_ + tile_size > capacity_bytes_ &&
         !lru_list_.empty()) {
    EvictLru();
  }

  // Add new entry
  lru_list_.push_front(key);
  cache_[key] =
      CacheEntry{.key = key, .tile = tile, .lru_it = lru_list_.begin()};
  current_size_bytes_ += tile_size;
}

void LRUTileCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
  lru_list_.clear();
  current_size_bytes_ = 0;
  hits_ = 0;
  misses_ = 0;
}

void LRUTileCache::EvictLru() {
  if (lru_list_.empty()) {
    return;
  }

  TileKey oldest_key = lru_list_.back();
  auto it = cache_.find(oldest_key);
  if (it != cache_.end()) {
    current_size_bytes_ -= it->second.tile->GetMemoryUsage();
    cache_.erase(it);
  }
  lru_list_.pop_back();
}

size_t LRUTileCache::GetSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.size();
}

size_t LRUTileCache::GetCapacity() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return capacity_bytes_;
}

size_t LRUTileCache::GetMemoryUsage() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_size_bytes_;
}

ITileCache::Stats LRUTileCache::GetStats() const {
  std::lock_guard<std::mutex> lock(mutex_);

  double hit_ratio = 0.0;
  size_t total_accesses = hits_ + misses_;
  if (total_accesses > 0) {
    hit_ratio = static_cast<double>(hits_) / total_accesses;
  }

  return Stats{.capacity = capacity_bytes_,
               .size = cache_.size(),
               .hits = hits_,
               .misses = misses_,
               .hit_ratio = hit_ratio,
               .memory_usage_bytes = current_size_bytes_};
}

aifocore::Status LRUTileCache::SetCapacity(size_t capacity_bytes) {
  if (capacity_bytes == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Cache capacity must be greater than 0");
  }

  std::lock_guard<std::mutex> lock(mutex_);

  capacity_bytes_ = capacity_bytes;

  // Evict if new capacity is smaller
  while (current_size_bytes_ > capacity_bytes_ && !lru_list_.empty()) {
    EvictLru();
  }

  return aifocore::Status::OkStatus();
}

}  // namespace runtime
}  // namespace fastslide
