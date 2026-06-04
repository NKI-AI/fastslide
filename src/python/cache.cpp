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

#include "aifocore/status/result.h"
#include "fastslide/runtime/global_cache_manager.h"

namespace fastslide::python {

// CacheManager implementation
CacheManager::CacheManager(std::shared_ptr<ITileCache> cache)
    : cache_(std::move(cache)) {}

aifocore::Result<std::shared_ptr<CacheManager>> CacheManager::Create(
    size_t capacity_bytes) {
  std::shared_ptr<LRUTileCache> cache;
  AIFOCORE_ASSIGN_OR_RETURN(cache, LRUTileCache::Create(capacity_bytes));
  return std::shared_ptr<CacheManager>(new CacheManager(std::move(cache)));
}

void CacheManager::SetCache(std::shared_ptr<ITileCache> cache) {
  cache_ = std::move(cache);
}

std::shared_ptr<ITileCache> CacheManager::GetCache() const {
  return cache_;
}

void CacheManager::Clear() {
  cache_->Clear();
}

ITileCache::Stats CacheManager::GetBasicStats() const {
  return cache_->GetStats();
}

CacheInspectionStats CacheManager::GetDetailedStats() const {
  auto basic = cache_->GetStats();
  CacheInspectionStats detailed;
  detailed.capacity_bytes = basic.capacity_bytes;
  detailed.size = basic.size;
  detailed.hits = basic.hits;
  detailed.misses = basic.misses;
  detailed.hit_ratio = basic.hit_ratio;
  detailed.memory_usage_mb = basic.memory_usage_bytes / (1024.0 * 1024.0);
  // TODO(jonasteuwen): Add actual key tracking if needed
  return detailed;
}

aifocore::Status CacheManager::Resize(size_t new_capacity_bytes) {
  std::shared_ptr<LRUTileCache> new_cache;
  AIFOCORE_ASSIGN_OR_RETURN(new_cache,
                            LRUTileCache::Create(new_capacity_bytes));
  cache_ = std::move(new_cache);
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide::python
