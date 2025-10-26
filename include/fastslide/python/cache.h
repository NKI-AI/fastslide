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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/utilities/cache.h"

namespace fastslide::python {

using fastslide::GlobalTileCache;
using fastslide::TileCache;

/// @brief Enhanced cache statistics for Python inspection
struct CacheInspectionStats {
  size_t capacity;
  size_t size;
  size_t hits;
  size_t misses;
  double hit_ratio;
  double memory_usage_mb;
  std::vector<std::string> recent_keys;
  std::unordered_map<std::string, size_t> key_frequencies;
};

/// @brief Cache manager with inspection capabilities
class CacheManager {
 private:
  std::shared_ptr<TileCache> cache_;

 public:
  /// @brief Create a CacheManager with given capacity
  /// @param capacity Cache capacity
  /// @return StatusOr containing the CacheManager
  [[nodiscard]] static absl::StatusOr<std::shared_ptr<CacheManager>> Create(
      size_t capacity = 1000);

  [[nodiscard]] std::shared_ptr<TileCache> GetCache() const;

  void Clear();

  [[nodiscard]] TileCache::Stats GetBasicStats() const;

  [[nodiscard]] CacheInspectionStats GetDetailedStats() const;

  [[nodiscard]] absl::Status Resize(size_t new_capacity);

 private:
  explicit CacheManager(std::shared_ptr<TileCache> cache);
};

/// @brief Global cache manager singleton
class GlobalCacheManager {
 public:
  [[nodiscard]] static GlobalCacheManager& Instance();

  [[nodiscard]] std::shared_ptr<TileCache> GetCache();

  [[nodiscard]] absl::Status SetCapacity(size_t capacity);

  [[nodiscard]] TileCache::Stats GetStats();

  [[nodiscard]] CacheInspectionStats GetDetailedStats();

  void Clear();
};

}  // namespace fastslide::python
