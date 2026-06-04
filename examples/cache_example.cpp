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

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <CLI11/CLI11.hpp>

#include "fastslide/fastslide.h"
#include "fastslide/runtime/lru_tile_cache.h"
#include "fastslide/runtime/reader_registry.h"

int main(int argc, char** argv) {
  CLI::App app{"FastSlide Cache Example"};

  std::string path;
  app.add_option("path", path, "Path to slide image")->required();

  CLI11_PARSE(app, argc, argv);

  std::cout << "Opening slide: " << path << "\n";

  // 1. Initialize a custom cache manager
  // In C++, the LRU cache capacity is specified in bytes.
  // For typical 512x512 RGB tiles (approx 750KB), 1000 tiles is ~750MB.
  const size_t kCacheCapacityBytes = 750ULL * 1024 * 1024;  // 750 MB

  auto cache_or = fastslide::runtime::LRUTileCache::Create(kCacheCapacityBytes);
  if (!cache_or.ok()) {
    std::cerr << "Failed to create cache: " << cache_or.status() << "\n";
    return 1;
  }
  auto cache = std::move(*cache_or);
  cache->Clear();

  std::cout << "Created custom cache with capacity: "
            << (kCacheCapacityBytes / (1024 * 1024)) << " MB\n";

  // Open the slide
  auto reader_or = fastslide::runtime::GetGlobalRegistry().CreateReader(path);
  if (!reader_or.ok()) {
    std::cerr << "Failed to open slide: " << reader_or.status() << "\n";
    return 1;
  }
  auto reader = std::move(*reader_or);

  // 2. Attach the cache manager to the slide
  reader->SetCache(cache);
  std::cout << "Cache enabled: "
            << (reader->IsCacheEnabled() ? "True" : "False") << "\n";

  const int level_count = reader->GetLevelCount();
  auto level0_info = reader->GetLevelInfo(0);
  if (!level0_info.ok()) {
    std::cerr << "Failed to get level 0 info: " << level0_info.status() << "\n";
    return 1;
  }
  const auto dims = level0_info->dimensions;
  const auto& props = reader->GetProperties();
  const auto& bounds = props.bounds;

  std::cout << "Dimensions: " << dims[0] << " x " << dims[1] << "\n";
  std::cout << "Levels: " << level_count << "\n";
  std::cout << "Resolution (mpp): " << props.mpp[0] << ", " << props.mpp[1]
            << " microns/pixel\n";
  std::cout << "Format: " << reader->GetFormatName() << "\n";
  std::cout << "Bounds (tissue region):\n";
  std::cout << "  x: " << bounds.x << "\n";
  std::cout << "  y: " << bounds.y << "\n";
  std::cout << "  width: " << bounds.width << "\n";
  std::cout << "  height: " << bounds.height << "\n";

  fastslide::ImageCoordinate location = {static_cast<uint32_t>(bounds.x),
                                         static_cast<uint32_t>(bounds.y)};
  int level = 0;
  fastslide::ImageDimensions size = {2048, 2048};

  std::cout << "\n--- First Read (Cold Cache) ---\n";
  auto start_time = std::chrono::high_resolution_clock::now();

  fastslide::RegionSpec region_spec{
      .top_left = location, .size = size, .level = level};

  // Clamp region to be safe, though pure read should handle out of bounds
  // gracefully depending on implementation, but here we want to mimic python
  // script behavior. The python script just calls read_region.
  auto region_or = reader->ReadRegion(region_spec);
  if (!region_or.ok()) {
    std::cerr << "Read failed: " << region_or.status() << "\n";
    return 1;
  }
  const auto& region = *region_or;

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end_time - start_time;

  std::cout << "Read time: " << std::fixed << std::setprecision(4)
            << elapsed.count() << "s\n";
  std::cout << "Image: " << region.GetWidth() << "x" << region.GetHeight()
            << ", " << region.GetChannels() << " channels, "
            << fastslide::GetName(region.GetDataType()) << "\n";

  // Check cache stats
  auto stats = cache->GetStats();
  std::cout << "Cache stats: tiles=" << stats.size
            << ", capacity=" << (stats.capacity_bytes / (1024 * 1024))
            << " MB, hits=" << stats.hits << ", misses=" << stats.misses
            << "\n";
  std::cout << "Memory usage: "
            << (static_cast<double>(stats.memory_usage_bytes) /
                (1024.0 * 1024.0))
            << " MB\n";

  std::cout << "\n--- Second Read (Hot Cache) ---\n";
  start_time = std::chrono::high_resolution_clock::now();

  // Reading the exact same region should be much faster as internal tiles are
  // cached
  auto region_cached_or = reader->ReadRegion(region_spec);
  if (!region_cached_or.ok()) {
    std::cerr << "Read failed: " << region_cached_or.status() << "\n";
    return 1;
  }
  const auto& region_cached = *region_cached_or;

  end_time = std::chrono::high_resolution_clock::now();
  elapsed = end_time - start_time;

  std::cout << "Read time: " << std::fixed << std::setprecision(4)
            << elapsed.count() << "s\n";

  // Check cache stats again
  stats = cache->GetStats();
  std::cout << "Cache stats: tiles=" << stats.size
            << ", capacity=" << (stats.capacity_bytes / (1024 * 1024))
            << " MB, hits=" << stats.hits << ", misses=" << stats.misses
            << "\n";
  std::cout << "Hit ratio: " << std::fixed << std::setprecision(2)
            << stats.hit_ratio << "\n";

  std::cout << "\n--- Clearing Cache ---\n";
  cache->Clear();
  stats = cache->GetStats();
  std::cout << "Cache stats after clear: Size=" << stats.size
            << ", Hits=" << stats.hits << ", Misses=" << stats.misses << "\n";

  std::cout << "\n--- Third Read (Should be Cold) ---\n";
  start_time = std::chrono::high_resolution_clock::now();

  // Read again after clear - should be cold
  auto region_third_or = reader->ReadRegion(region_spec);
  if (!region_third_or.ok()) {
    std::cerr << "Read failed: " << region_third_or.status() << "\n";
    return 1;
  }
  const auto& region_third = *region_third_or;

  end_time = std::chrono::high_resolution_clock::now();
  elapsed = end_time - start_time;

  std::cout << "Read time: " << std::fixed << std::setprecision(4)
            << elapsed.count() << "s\n";

  stats = cache->GetStats();
  std::cout << "Cache stats: tiles=" << stats.size
            << ", capacity=" << (stats.capacity_bytes / (1024 * 1024))
            << " MB, hits=" << stats.hits << ", misses=" << stats.misses
            << "\n";

  // Access associated data

  std::cout << "\n--- Third Read (Cold Cache) ---\n";
  start_time = std::chrono::high_resolution_clock::now();

  auto region_cold_or = reader->ReadRegion(region_spec);
  if (!region_cold_or.ok()) {
    std::cerr << "Read failed: " << region_cold_or.status() << "\n";
    return 1;
  }
  const auto& region_cold = *region_cold_or;

  end_time = std::chrono::high_resolution_clock::now();
  elapsed = end_time - start_time;

  std::cout << "Read time: " << std::fixed << std::setprecision(4)
            << elapsed.count() << "s\n";

  std::cout << "Image: " << region_cold.GetWidth() << "x"
            << region_cold.GetHeight() << ", " << region_cold.GetChannels()
            << " channels, " << fastslide::GetName(region_cold.GetDataType())
            << "\n";

  std::cout << "Cache stats: tiles=" << stats.size
            << ", capacity=" << (stats.capacity_bytes / (1024 * 1024))
            << " MB, hits=" << stats.hits << ", misses=" << stats.misses
            << "\n";
  std::cout << "Hit ratio: " << std::fixed << std::setprecision(2)
            << stats.hit_ratio << "\n";
  return 0;
}
