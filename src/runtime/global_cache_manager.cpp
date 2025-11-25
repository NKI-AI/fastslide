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

#include "fastslide/runtime/global_cache_manager.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/lru_tile_cache.h"

namespace fastslide {
namespace runtime {

GlobalCacheManager::GlobalCacheManager() {
  // Create default LRU cache with 1000 tile capacity
  auto cache_result = LRUTileCache::Create(1000);
  if (cache_result.ok()) {
    cache_ = std::move(*cache_result);
  } else {
    // This should never happen with valid capacity, but handle gracefully
    cache_ = nullptr;
  }
}

GlobalCacheManager& GlobalCacheManager::Instance() {
  static GlobalCacheManager instance;
  return instance;
}

std::shared_ptr<ITileCache> GlobalCacheManager::GetCache() {
  absl::MutexLock lock(&mutex_);
  return cache_;
}

void GlobalCacheManager::SetCache(std::shared_ptr<ITileCache> cache) {
  absl::MutexLock lock(&mutex_);
  cache_ = std::move(cache);
}

absl::Status GlobalCacheManager::SetCapacity(size_t capacity) {
  absl::MutexLock lock(&mutex_);

  // Create new LRU cache with new capacity
  auto cache_result = LRUTileCache::Create(capacity);
  if (!cache_result.ok()) {
    return cache_result.status();
  }
  cache_ = std::move(*cache_result);

  return absl::OkStatus();
}

size_t GlobalCacheManager::GetCapacity() const {
  absl::MutexLock lock(&mutex_);
  return cache_ ? cache_->GetCapacity() : 0;
}

size_t GlobalCacheManager::GetSize() const {
  absl::MutexLock lock(&mutex_);
  return cache_ ? cache_->GetSize() : 0;
}

ITileCache::Stats GlobalCacheManager::GetStats() const {
  absl::MutexLock lock(&mutex_);
  if (cache_) {
    return cache_->GetStats();
  }
  return ITileCache::Stats{};
}

void GlobalCacheManager::Clear() {
  absl::MutexLock lock(&mutex_);
  if (cache_) {
    cache_->Clear();
  }
}

}  // namespace runtime
}  // namespace fastslide
