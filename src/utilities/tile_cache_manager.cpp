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

#include "fastslide/utilities/tile_cache_manager.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/statusor.h"

namespace fastslide {

TileCacheManager::TileCacheManager(std::shared_ptr<TileCache> cache)
    : cache_(std::move(cache)) {}

absl::StatusOr<std::shared_ptr<CachedTile>> TileCacheManager::GetTile(
    const std::string& filename, uint16_t level,
    const aifocore::Size<uint32_t, 2>& tile_coords, TileLoader loader) {
  // Try to get from cache first
  auto cached_tile =
      GetTileFromCache(filename, level, tile_coords[0], tile_coords[1]);
  if (cached_tile) {
    return cached_tile;
  }

  auto tile_result = loader();
  if (!tile_result.ok()) {
    return tile_result.status();
  }

  auto tile = tile_result.value();

  // Store in cache if caching is enabled
  if (IsCacheEnabled()) {
    PutTile(filename, level, tile_coords[0], tile_coords[1], tile);
  }

  return tile;
}

std::shared_ptr<CachedTile> TileCacheManager::GetTileFromCache(
    const std::string& filename, uint16_t level, uint32_t tile_x,
    uint32_t tile_y) {
  if (!IsCacheEnabled()) {
    return nullptr;
  }

  runtime::TileKey key = CreateCacheKey(filename, level, tile_x, tile_y);
  return cache_->Get(key);
}

void TileCacheManager::PutTile(const std::string& filename, uint16_t level,
                               uint32_t tile_x, uint32_t tile_y,
                               std::shared_ptr<CachedTile> tile) {
  if (!IsCacheEnabled()) {
    return;
  }

  runtime::TileKey key = CreateCacheKey(filename, level, tile_x, tile_y);
  cache_->Put(key, std::move(tile));
}

bool TileCacheManager::IsCacheEnabled() const {
  return cache_ != nullptr;
}

void TileCacheManager::SetCache(std::shared_ptr<TileCache> cache) {
  cache_ = std::move(cache);
}

std::shared_ptr<TileCache> TileCacheManager::GetCache() const {
  return cache_;
}

TileCache::Stats TileCacheManager::GetStats() const {
  if (!IsCacheEnabled()) {
    return TileCache::Stats{.capacity = 0,
                            .size = 0,
                            .hits = 0,
                            .misses = 0,
                            .hit_ratio = 0.0,
                            .memory_usage_bytes = 0};
  }
  return cache_->GetStats();
}

void TileCacheManager::Clear() {
  if (IsCacheEnabled()) {
    cache_->Clear();
  }
}

runtime::TileKey TileCacheManager::CreateCacheKey(const std::string& filename,
                                                  uint16_t level,
                                                  uint32_t tile_x,
                                                  uint32_t tile_y) const {
  return runtime::TileKey(filename, level, tile_x, tile_y);
}

}  // namespace fastslide
