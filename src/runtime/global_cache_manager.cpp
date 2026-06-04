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
#include <mutex>
#include <utility>

#include "aifocore/status/result.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/lru_tile_cache.h"

namespace fastslide {
namespace runtime {

namespace {
// Default global cache capacity: 1 GiB. Matches LRUTileCache::Create()'s
// default and is sized so that the typical 256 KiB-1 MiB tiles can fit a
// few thousand entries.
constexpr size_t kDefaultGlobalCacheCapacityBytes = static_cast<size_t>(1)
                                                    << 30;
}  // namespace

GlobalCacheManager::GlobalCacheManager() {
  auto cache_result = LRUTileCache::Create(kDefaultGlobalCacheCapacityBytes);
  if (cache_result.ok()) {
    cache_ = std::move(*cache_result);
  } else {
    // Should not happen with a valid non-zero capacity, but be defensive.
    cache_ = nullptr;
  }
}

GlobalCacheManager& GlobalCacheManager::Instance() {
  static GlobalCacheManager instance;
  return instance;
}

std::shared_ptr<ITileCache> GlobalCacheManager::GetCache() {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_;
}

void GlobalCacheManager::SetCache(std::shared_ptr<ITileCache> cache) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_ = std::move(cache);
}

aifocore::Status GlobalCacheManager::SetCapacityBytes(size_t capacity_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);

  AIFOCORE_ASSIGN_OR_RETURN(cache_, LRUTileCache::Create(capacity_bytes));

  return aifocore::Status::OkStatus();
}

size_t GlobalCacheManager::GetCapacityBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_ ? cache_->GetCapacityBytes() : 0;
}

size_t GlobalCacheManager::GetSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_ ? cache_->GetSize() : 0;
}

ITileCache::Stats GlobalCacheManager::GetStats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (cache_) {
    return cache_->GetStats();
  }
  return ITileCache::Stats{};
}

void GlobalCacheManager::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (cache_) {
    cache_->Clear();
  }
}

}  // namespace runtime
}  // namespace fastslide
