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

#include "fastslide/utilities/cache.h"

#include <memory>
#include <stdexcept>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/concepts/numeric.h"
#include "aifocore/status/status_macros.h"

namespace fastslide {

// TileCache implementation
absl::StatusOr<TileCache> TileCache::Create(size_t capacity) {
  if (capacity == 0) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Cache capacity must be greater than 0");
  }
  return absl::StatusOr<TileCache>(std::in_place, capacity);
}

absl::StatusOr<std::shared_ptr<TileCache>> TileCache::CreateShared(
    size_t capacity) {
  if (capacity == 0) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Cache capacity must be greater than 0");
  }
  return std::shared_ptr<TileCache>(new TileCache(capacity));
}

TileCache::TileCache(size_t capacity)
    : capacity_(capacity), cache_{}, lru_list_{}, hits_(0), misses_(0) {
  // Note: validation should be done in Create() method
  // Constructor assumes valid capacity for compatibility
}

std::shared_ptr<CachedTile> TileCache::Get(const TileKey& key) {
  absl::MutexLock lock(&mutex_);

  auto iter = cache_.find(key);
  if (iter == cache_.end()) {
    ++misses_;
    return nullptr;
  }

  ++hits_;

  // Update access time
  iter->second.tile->access_time = std::chrono::steady_clock::now();

  // Move to front of LRU list
  lru_list_.erase(iter->second.lru_it);
  lru_list_.push_front(key);
  iter->second.lru_it = lru_list_.begin();

  return iter->second.tile;
}

void TileCache::Put(const TileKey& key, std::shared_ptr<CachedTile> tile) {
  if (!tile) {
    return;  // Don't cache null tiles
  }

  absl::MutexLock lock(&mutex_);

  // Check if already exists
  auto iter = cache_.find(key);
  if (iter != cache_.end()) {
    // Update existing entry
    iter->second.tile = tile;
    iter->second.tile->access_time = std::chrono::steady_clock::now();

    // Move to front of LRU list
    lru_list_.erase(iter->second.lru_it);
    lru_list_.push_front(key);
    iter->second.lru_it = lru_list_.begin();
    return;
  }

  // Evict if at capacity
  if (cache_.size() >= capacity_) {
    EvictLru();
  }

  // Add new entry
  lru_list_.push_front(key);
  cache_[key] =
      CacheEntry{.key = key, .tile = tile, .lru_it = lru_list_.begin()};
}

void TileCache::Clear() {
  absl::MutexLock lock(&mutex_);
  cache_.clear();
  lru_list_.clear();
  hits_ = 0;
  misses_ = 0;
}

void TileCache::EvictLru() {
  if (lru_list_.empty()) {
    return;
  }

  TileKey oldest_key = lru_list_.back();
  lru_list_.pop_back();
  cache_.erase(oldest_key);
}

TileCache::Stats TileCache::GetStats() const {
  absl::MutexLock lock(&mutex_);

  double hit_ratio = 0.0;
  size_t total_accesses = hits_ + misses_;
  if (total_accesses > 0) {
    hit_ratio = static_cast<double>(hits_) / total_accesses;
  }

  // Calculate total memory usage
  size_t memory_usage_bytes = 0;
  for (const auto& entry : cache_) {
    if (entry.second.tile) {
      memory_usage_bytes += entry.second.tile->data.size();
    }
  }

  return Stats{.capacity = capacity_,
               .size = cache_.size(),
               .hits = hits_,
               .misses = misses_,
               .hit_ratio = hit_ratio,
               .memory_usage_bytes = memory_usage_bytes};
}

size_t TileCache::GetCapacity() const {
  absl::MutexLock lock(&mutex_);
  return capacity_;
}

size_t TileCache::GetSize() const {
  absl::MutexLock lock(&mutex_);
  return cache_.size();
}

size_t TileCache::GetMemoryUsage() const {
  absl::MutexLock lock(&mutex_);

  size_t memory_usage_bytes = 0;
  for (const auto& entry : cache_) {
    if (entry.second.tile) {
      memory_usage_bytes += entry.second.tile->data.size();
    }
  }

  return memory_usage_bytes;
}

absl::Status TileCache::SetCapacity(size_t capacity) {
  if (capacity == 0) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Cache capacity must be greater than 0");
  }

  absl::MutexLock lock(&mutex_);

  // Clear existing cache and update capacity
  cache_.clear();
  lru_list_.clear();
  capacity_ = capacity;
  hits_ = 0;
  misses_ = 0;

  return absl::OkStatus();
}

// GlobalTileCache implementation
GlobalTileCache::GlobalTileCache()
    : cache_(std::make_unique<TileCache>(1000)) {}

GlobalTileCache& GlobalTileCache::Instance() {
  static GlobalTileCache instance;
  return instance;
}

TileCache& GlobalTileCache::GetCache() {
  absl::MutexLock lock(&mutex_);
  return *cache_;
}

const TileCache& GlobalTileCache::GetCache() const {
  absl::MutexLock lock(&mutex_);
  return *cache_;
}

absl::Status GlobalTileCache::SetCapacity(size_t capacity) {
  absl::MutexLock lock(&mutex_);
  return cache_->SetCapacity(capacity);
}

size_t GlobalTileCache::GetCapacity() const {
  absl::MutexLock lock(&mutex_);
  return cache_->GetCapacity();
}

}  // namespace fastslide
