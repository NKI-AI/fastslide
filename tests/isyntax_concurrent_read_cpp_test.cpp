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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "fastslide/readers/isyntax/third_party/cache.h"
#include "fastslide/readers/isyntax/third_party/file.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/reader.h"
#include "fastslide/readers/isyntax/third_party/utils/block_allocator.h"

namespace {

struct TileRef {
  int32_t level;
  int32_t x;
  int32_t y;
};

uint64_t HashPixels(const std::vector<uint32_t>& pixels) {
  uint64_t hash = 1469598103934665603ull;  // FNV-1a
  const auto* bytes = reinterpret_cast<const uint8_t*>(pixels.data());
  for (size_t i = 0; i < pixels.size() * sizeof(uint32_t); ++i) {
    hash = (hash ^ bytes[i]) * 1099511628211ull;
  }
  return hash;
}

uint64_t ReadTileHash(const isyntax::IsyntaxFile& file, const TileRef& tile) {
  std::vector<uint32_t> pixels(static_cast<size_t>(file.tile_width()) *
                               file.tile_height());
  EXPECT_TRUE(file.ReadTile(tile.level, tile.x, tile.y, pixels,
                            isyntax::PixelFormat::kRgba)
                  .ok());
  return HashPixels(pixels);
}

size_t UsedBytes(const isyntax::IsyntaxFile& file) {
  return block_allocator_used_bytes(file.cache()->ll_coeff_block_allocator) +
         block_allocator_used_bytes(file.cache()->h_coeff_block_allocator);
}

// Tiles read by many threads sharing one file have the pixels of tiles read by
// a single thread. The cache holds fewer tiles than the threads need, so that
// they evict the tiles of each other while reading.
TEST(IsyntaxConcurrentReadTest, ThreadsSharingAFileReadTheSamePixels) {
  const char* path = std::getenv("FASTSLIDE_ISYNTAX_TEST_SLIDE");
  if (path == nullptr) {
    GTEST_SKIP() << "FASTSLIDE_ISYNTAX_TEST_SLIDE is not set";
  }
  constexpr int kThreads = 8;
  constexpr int32_t kWindow = 8;  // Tiles read per level, around the center.
  const size_t previous_capacity = isyntax::IsyntaxCache::SharedCapacityBytes();
  isyntax::IsyntaxCache::SetSharedCapacityBytes(size_t{32} << 20);

  auto reference_or = isyntax::IsyntaxFile::Open(path);
  ASSERT_TRUE(reference_or.ok());
  const auto& reference = **reference_or;
  std::vector<TileRef> tiles;
  for (int32_t level = 0; level < reference.level_count(); ++level) {
    const isyntax_level_t* l = reference.level(level);
    const int32_t x0 = std::max(0, l->width_in_tiles / 2 - kWindow / 2);
    const int32_t y0 = std::max(0, l->height_in_tiles / 2 - kWindow / 2);
    for (int32_t y = y0; y < std::min(l->height_in_tiles, y0 + kWindow); ++y) {
      for (int32_t x = x0; x < std::min(l->width_in_tiles, x0 + kWindow); ++x) {
        tiles.push_back({level, x, y});
      }
    }
  }
  std::vector<uint64_t> expected;
  for (const TileRef& tile : tiles) {
    expected.push_back(ReadTileHash(reference, tile));
  }

  auto shared_or = isyntax::IsyntaxFile::Open(path);
  ASSERT_TRUE(shared_or.ok());
  const auto& shared = **shared_or;
  std::atomic<int> mismatches{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      std::vector<size_t> order(tiles.size());
      for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
      }
      std::shuffle(order.begin(), order.end(), std::mt19937(1234 + t));
      for (size_t i : order) {
        if (ReadTileHash(shared, tiles[i]) != expected[i]) {
          ++mismatches;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(mismatches, 0) << "of " << kThreads * tiles.size() << " reads";
  isyntax::IsyntaxCache::SetSharedCapacityBytes(previous_capacity);
}

// Files share the cache; closing one gives its coefficients back.
TEST(IsyntaxConcurrentReadTest, ClosingAFileFreesItsCoefficients) {
  const char* path = std::getenv("FASTSLIDE_ISYNTAX_TEST_SLIDE");
  if (path == nullptr) {
    GTEST_SKIP() << "FASTSLIDE_ISYNTAX_TEST_SLIDE is not set";
  }
  auto kept_or = isyntax::IsyntaxFile::Open(path);
  ASSERT_TRUE(kept_or.ok());
  auto closed_or = isyntax::IsyntaxFile::Open(path);
  ASSERT_TRUE(closed_or.ok());
  ASSERT_EQ((*kept_or)->cache(), (*closed_or)->cache());
  const auto& kept = **kept_or;
  const isyntax_level_t* top = kept.level(kept.level_count() - 1);
  const TileRef tile{kept.level_count() - 1, top->width_in_tiles / 2,
                     top->height_in_tiles / 2};
  ReadTileHash(kept, tile);
  const size_t kept_bytes = UsedBytes(kept);

  ReadTileHash(**closed_or, tile);
  EXPECT_GT(UsedBytes(kept), kept_bytes);
  closed_or->reset();

  EXPECT_EQ(UsedBytes(kept), kept_bytes);
}

}  // namespace
