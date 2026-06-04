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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_GLOBAL_CACHE_MANAGER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_GLOBAL_CACHE_MANAGER_H_

#include <cstddef>
#include <memory>
#include <mutex>

#include "aifocore/status/result.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/lru_tile_cache.h"

/**
 * @file global_cache_manager.h
 * @brief Global cache manager singleton for application-wide tile caching
 *
 * This header provides a singleton manager for the global tile cache,
 * enabling all readers in an application to share a single cache instance.
 * This is the recommended approach for most applications as it maximizes
 * cache efficiency and simplifies cache management.
 */

namespace fastslide {
namespace runtime {

/// @brief Global cache manager singleton
///
/// Manages a single application-wide tile cache that can be shared by all
/// slide readers. This provides:
/// - Maximum cache efficiency (no duplicate cached tiles)
/// - Simplified cache management (single configuration point)
/// - Thread-safe access from multiple readers
/// - Consistent caching behavior across the application
///
/// The global cache uses the LRU eviction policy by default but can be
/// replaced with a custom ITileCache implementation if needed.
///
/// Example usage:
/// @code
/// // Configure global cache at application startup
/// auto& manager = GlobalCacheManager::Instance();
/// manager.SetCapacityBytes(static_cast<size_t>(2) << 30);  // 2 GiB
///
/// // All readers will automatically use this cache when enabled
/// auto reader = registry.CreateReader("slide.mrxs");
/// @endcode
class GlobalCacheManager {
 public:
  /// @brief Get the singleton instance
  ///
  /// Returns the global singleton instance of the cache manager. The instance
  /// is created on first access with a default capacity of 1 GiB.
  ///
  /// @return Reference to the global cache manager
  /// @note Thread-safe
  static GlobalCacheManager& Instance();

  /// @brief Get the global tile cache
  ///
  /// Returns a shared pointer to the global cache that can be injected into
  /// readers. The returned pointer uses a non-owning deleter to prevent
  /// premature destruction of the singleton cache.
  ///
  /// @return Shared pointer to the global cache (non-owning)
  /// @note Thread-safe
  [[nodiscard]] std::shared_ptr<ITileCache> GetCache();

  /// @brief Replace the global cache with a custom implementation
  ///
  /// Allows replacing the default LRU cache with a custom ITileCache
  /// implementation (e.g., GPU cache, shared memory cache, distributed cache).
  ///
  /// @param cache New cache implementation
  /// @note Thread-safe. Clears the existing cache.
  void SetCache(std::shared_ptr<ITileCache> cache);

  /// @brief Set the capacity of the global cache in bytes
  ///
  /// Replaces the current cache with a new LRU cache of the requested byte
  /// capacity. All previously cached tiles are dropped.
  ///
  /// @param capacity_bytes New cache capacity in bytes
  /// @return Status indicating success or failure
  /// @note Thread-safe. Replaces the existing cache (clears all entries).
  [[nodiscard]] aifocore::Status SetCapacityBytes(size_t capacity_bytes);

  /// @brief Get current cache capacity in bytes
  /// @return Maximum cache capacity in bytes
  /// @note Thread-safe
  [[nodiscard]] size_t GetCapacityBytes() const;

  /// @brief Get current cache size
  /// @return Current number of cached tiles
  /// @note Thread-safe
  [[nodiscard]] size_t GetSize() const;

  /// @brief Get cache statistics
  /// @return Current cache statistics
  /// @note Thread-safe
  [[nodiscard]] ITileCache::Stats GetStats() const;

  /// @brief Clear all cached tiles
  ///
  /// Removes all tiles from the cache while preserving capacity settings.
  ///
  /// @note Thread-safe
  void Clear();

 private:
  /// @brief Private constructor for singleton
  GlobalCacheManager();

  /// @brief Destructor
  ~GlobalCacheManager() = default;

  // Non-copyable, non-movable
  GlobalCacheManager(const GlobalCacheManager&) = delete;
  GlobalCacheManager& operator=(const GlobalCacheManager&) = delete;
  GlobalCacheManager(GlobalCacheManager&&) = delete;
  GlobalCacheManager& operator=(GlobalCacheManager&&) = delete;

  mutable std::mutex mutex_;           ///< Mutex for thread safety
  std::shared_ptr<ITileCache> cache_;  ///< The global cache instance
};

}  // namespace runtime

// Import into fastslide namespace
using runtime::GlobalCacheManager;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_GLOBAL_CACHE_MANAGER_H_
