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

// Correctness tests for the C API tile-cache surface. These require a real
// slide, provided via FASTSLIDE_BENCHMARK_FILE; the tests are skipped when it
// is unset so the suite stays hermetic by default.

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "fastslide/c/fastslide.h"
#include "gtest/gtest.h"

namespace {

const char* BenchmarkFile() {
  return std::getenv("FASTSLIDE_BENCHMARK_FILE");
}

// Reads a 256x256 level-0 region and returns its raw bytes, or empty on error.
std::vector<uint8_t> ReadRegionBytes(FastSlideSlideReader* reader) {
  FastSlideImage* image = fastslide_slide_reader_read_region_coords(
      reader, /*x=*/0, /*y=*/0, /*width=*/256, /*height=*/256, /*level=*/0,
      /*z=*/0, /*t=*/0);
  if (image == nullptr) {
    return {};
  }
  const uint8_t* data = fastslide_image_get_data(image);
  const size_t size = fastslide_image_get_size_bytes(image);
  std::vector<uint8_t> bytes;
  if (data != nullptr && size > 0) {
    bytes.assign(data, data + size);
  }
  fastslide_image_free(image);
  return bytes;
}

class CacheCApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (BenchmarkFile() == nullptr) {
      GTEST_SKIP() << "Set FASTSLIDE_BENCHMARK_FILE to a supported slide to "
                      "run the C API cache tests.";
    }
    ASSERT_EQ(fastslide_registry_initialize(), 1);
  }
};

TEST_F(CacheCApiTest, CacheProducesIdenticalPixelsAndHits) {
  FastSlideSlideReader* reader = fastslide_create_reader_with_cache(
      BenchmarkFile(), /*cache_capacity_bytes=*/static_cast<size_t>(256) << 20);
  ASSERT_NE(reader, nullptr) << fastslide_get_last_error();
  EXPECT_EQ(fastslide_slide_reader_is_cache_enabled(reader), 1);

  const std::vector<uint8_t> first = ReadRegionBytes(reader);
  ASSERT_FALSE(first.empty());

  FastSlideCacheStats after_first{};
  ASSERT_EQ(fastslide_slide_reader_get_cache_stats(reader, &after_first), 1);
  EXPECT_GT(after_first.misses, 0u)
      << "cold read should populate the cache (misses)";

  const std::vector<uint8_t> second = ReadRegionBytes(reader);
  ASSERT_EQ(first.size(), second.size());
  EXPECT_EQ(first, second) << "cached read must be byte-identical";

  FastSlideCacheStats after_second{};
  ASSERT_EQ(fastslide_slide_reader_get_cache_stats(reader, &after_second), 1);
  EXPECT_GT(after_second.hits, 0u)
      << "repeated read of the same tiles should hit the cache";

  fastslide_slide_reader_free(reader);
}

TEST_F(CacheCApiTest, NoCacheReportsDisabled) {
  FastSlideSlideReader* reader = fastslide_create_reader(BenchmarkFile());
  ASSERT_NE(reader, nullptr) << fastslide_get_last_error();

  EXPECT_EQ(fastslide_slide_reader_is_cache_enabled(reader), 0);

  FastSlideCacheStats stats{};
  EXPECT_EQ(fastslide_slide_reader_get_cache_stats(reader, &stats), 0)
      << "no cache attached: stats query should fail";

  fastslide_slide_reader_free(reader);
}

TEST_F(CacheCApiTest, SetCacheZeroDisables) {
  FastSlideSlideReader* reader = fastslide_create_reader_with_cache(
      BenchmarkFile(), static_cast<size_t>(64) << 20);
  ASSERT_NE(reader, nullptr) << fastslide_get_last_error();
  EXPECT_EQ(fastslide_slide_reader_is_cache_enabled(reader), 1);

  EXPECT_EQ(fastslide_slide_reader_set_cache(reader, 0), 1);
  EXPECT_EQ(fastslide_slide_reader_is_cache_enabled(reader), 0);

  fastslide_slide_reader_free(reader);
}

TEST_F(CacheCApiTest, GlobalCacheConfigurable) {
  ASSERT_EQ(
      fastslide_global_cache_set_capacity_bytes(static_cast<size_t>(128) << 20),
      1);
  fastslide_global_cache_clear();

  FastSlideSlideReader* reader = fastslide_create_reader(BenchmarkFile());
  ASSERT_NE(reader, nullptr) << fastslide_get_last_error();
  ASSERT_EQ(fastslide_slide_reader_use_global_cache(reader), 1);
  EXPECT_EQ(fastslide_slide_reader_is_cache_enabled(reader), 1);

  (void)ReadRegionBytes(reader);
  (void)ReadRegionBytes(reader);

  FastSlideCacheStats global_stats{};
  ASSERT_EQ(fastslide_global_cache_get_stats(&global_stats), 1);
  EXPECT_EQ(global_stats.capacity_bytes, static_cast<size_t>(128) << 20);

  fastslide_slide_reader_free(reader);
}

}  // namespace
