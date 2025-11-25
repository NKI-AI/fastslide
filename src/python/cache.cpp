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

#include "fastslide/python/cache.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"

namespace fastslide::python {

// CacheManager implementation
CacheManager::CacheManager(std::shared_ptr<TileCache> cache)
    : cache_(std::move(cache)) {}

absl::StatusOr<std::shared_ptr<CacheManager>> CacheManager::Create(
    size_t capacity) {
  std::shared_ptr<TileCache> cache;
  ASSIGN_OR_RETURN(cache, TileCache::CreateShared(capacity),
                   "Failed to create tile cache");
  return std::shared_ptr<CacheManager>(new CacheManager(std::move(cache)));
}

std::shared_ptr<TileCache> CacheManager::GetCache() const {
  return cache_;
}

void CacheManager::Clear() {
  cache_->Clear();
}

TileCache::Stats CacheManager::GetBasicStats() const {
  return cache_->GetStats();
}

CacheInspectionStats CacheManager::GetDetailedStats() const {
  auto basic = cache_->GetStats();
  CacheInspectionStats detailed;
  detailed.capacity = basic.capacity;
  detailed.size = basic.size;
  detailed.hits = basic.hits;
  detailed.misses = basic.misses;
  detailed.hit_ratio = basic.hit_ratio;
  detailed.memory_usage_mb =
      basic.memory_usage_bytes / (1024.0 * 1024.0);  // Convert bytes to MB
  // TODO(jonasteuwen): Add actual key tracking if needed
  return detailed;
}

absl::Status CacheManager::Resize(size_t new_capacity) {
  // Create new cache with new capacity
  std::shared_ptr<TileCache> new_cache;
  ASSIGN_OR_RETURN(new_cache, TileCache::CreateShared(new_capacity),
                   "Failed to create new tile cache");
  cache_ = std::move(new_cache);
  return absl::OkStatus();
}

// GlobalCacheManager implementation
GlobalCacheManager& GlobalCacheManager::Instance() {
  static GlobalCacheManager instance;
  return instance;
}

std::shared_ptr<TileCache> GlobalCacheManager::GetCache() {
  return std::shared_ptr<TileCache>(&GlobalTileCache::Instance().GetCache(),
                                    [](TileCache*) {});
}

absl::Status GlobalCacheManager::SetCapacity(size_t capacity) {
  return GlobalTileCache::Instance().SetCapacity(capacity);
}

TileCache::Stats GlobalCacheManager::GetStats() {
  return GlobalTileCache::Instance().GetCache().GetStats();
}

CacheInspectionStats GlobalCacheManager::GetDetailedStats() {
  auto basic = GlobalTileCache::Instance().GetCache().GetStats();
  CacheInspectionStats detailed;
  detailed.capacity = basic.capacity;
  detailed.size = basic.size;
  detailed.hits = basic.hits;
  detailed.misses = basic.misses;
  detailed.hit_ratio = basic.hit_ratio;
  detailed.memory_usage_mb =
      basic.memory_usage_bytes / (1024.0 * 1024.0);  // Convert bytes to MB
  // TODO(jonasteuwen): Add actual key tracking if needed
  return detailed;
}

void GlobalCacheManager::Clear() {
  GlobalTileCache::Instance().GetCache().Clear();
}

}  // namespace fastslide::python
