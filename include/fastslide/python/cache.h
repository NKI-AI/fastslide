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

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/lru_tile_cache.h"

namespace fastslide::python {

using fastslide::runtime::ITileCache;
using fastslide::runtime::LRUTileCache;

/// @brief Enhanced cache statistics for Python inspection
struct CacheInspectionStats {
  size_t capacity_bytes;
  size_t size;
  size_t hits;
  size_t misses;
  double hit_ratio;
  double memory_usage_mb;
  std::vector<std::string> recent_keys;
  std::unordered_map<std::string, size_t> key_frequencies;
};

/// @brief Default capacity for Python-created caches: 1 GiB.
inline constexpr size_t kDefaultCacheManagerCapacityBytes =
    static_cast<size_t>(1) << 30;

/// @brief Cache manager with inspection capabilities
class CacheManager {
 private:
  std::shared_ptr<ITileCache> cache_;

 public:
  /// @brief Create a CacheManager with given capacity in bytes
  /// @param capacity_bytes Cache capacity in bytes (default 1 GiB)
  /// @return Result containing the CacheManager
  [[nodiscard]] static aifocore::Result<std::shared_ptr<CacheManager>> Create(
      size_t capacity_bytes = kDefaultCacheManagerCapacityBytes);

  /// @brief Set the cache implementation directly (e.g. dependency injection)
  /// @param cache The cache implementation
  void SetCache(std::shared_ptr<ITileCache> cache);

  [[nodiscard]] std::shared_ptr<ITileCache> GetCache() const;

  void Clear();

  [[nodiscard]] ITileCache::Stats GetBasicStats() const;

  [[nodiscard]] CacheInspectionStats GetDetailedStats() const;

  /// @brief Resize the underlying cache by replacing it with a new LRU cache.
  /// @param new_capacity_bytes New capacity in bytes
  [[nodiscard]] aifocore::Status Resize(size_t new_capacity_bytes);

 private:
  explicit CacheManager(std::shared_ptr<ITileCache> cache);
};

}  // namespace fastslide::python
