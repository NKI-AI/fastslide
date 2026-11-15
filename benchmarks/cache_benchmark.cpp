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

// Benchmark for the internal tile cache: compares repeated overlapping
// region reads with and without a decode cache attached. Overlapping windows
// deliberately land inside the same native tile-grid cells so a cache turns
// the repeated decodes into hits.
//
// Provide a slide via the environment variable, e.g.:
//   FASTSLIDE_BENCHMARK_FILE=/abs/path/CMU-3.ndpi \
//       bazelisk run @fastslide/benchmarks:cache_benchmark

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "benchmark/benchmark.h"
#include "fastslide/runtime/lru_tile_cache.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/slide_reader.h"

namespace {

// Slide path from FASTSLIDE_BENCHMARK_FILE (default: "CMU-3.ndpi", expected to
// be resolvable from the working directory; prefer an absolute path).
const char* GetBenchmarkFilePath() {
  const char* env_path = std::getenv("FASTSLIDE_BENCHMARK_FILE");
  return env_path != nullptr ? env_path : "CMU-3.ndpi";
}

// A single read window at a given level.
struct Window {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

// Build a set of overlapping windows anchored near the origin of `level` that
// all fall within a 2x2 native-tile footprint, so repeated reads reuse the
// same decoded tiles. `window` is the read size (e.g. 256), `step` the offset
// stride (e.g. 64) producing heavy overlap.
std::vector<Window> BuildOverlappingWindows(uint32_t level_width,
                                            uint32_t level_height,
                                            uint32_t tile_size, uint32_t window,
                                            uint32_t step) {
  std::vector<Window> windows;
  // Footprint spanning two native tiles in each dimension (bounded by level).
  const uint32_t span = std::min<uint32_t>(2 * tile_size, level_width);
  const uint32_t span_y = std::min<uint32_t>(2 * tile_size, level_height);
  for (uint32_t oy = 0; oy + window <= span_y; oy += step) {
    for (uint32_t ox = 0; ox + window <= span; ox += step) {
      windows.push_back(Window{ox, oy, window, window});
    }
  }
  if (windows.empty()) {
    // Level smaller than a single window: fall back to one clamped read.
    windows.push_back(Window{0, 0, std::min(window, level_width),
                             std::min(window, level_height)});
  }
  return windows;
}

// Opens the benchmark slide and caches the level-0 geometry. A fresh reader is
// created per fixture instance so cache attachment is isolated between the
// no-cache and with-cache variants.
class ReaderContext {
 public:
  bool Open() {
    auto reader_or = fastslide::runtime::GetGlobalRegistry().CreateReader(
        GetBenchmarkFilePath());
    if (!reader_or.ok()) {
      return false;
    }
    reader_ = std::move(reader_or.value());

    auto level_info_or = reader_->GetLevelInfo(0);
    if (!level_info_or.ok()) {
      return false;
    }
    const auto level_info = level_info_or.value();
    level_width_ = level_info.dimensions[0];
    level_height_ = level_info.dimensions[1];

    const auto tile = reader_->GetTileSize();
    tile_size_ = tile[0] != 0 ? tile[0] : 256;
    return true;
  }

  fastslide::SlideReader* reader() const { return reader_.get(); }

  uint32_t level_width() const { return level_width_; }

  uint32_t level_height() const { return level_height_; }

  uint32_t tile_size() const { return tile_size_; }

 private:
  std::unique_ptr<fastslide::SlideReader> reader_;
  uint32_t level_width_{0};
  uint32_t level_height_{0};
  uint32_t tile_size_{256};
};

// Reads every window once and returns bytes processed, or -1 on error.
int64_t ReadWindows(const fastslide::SlideReader& reader,
                    const std::vector<Window>& windows) {
  int64_t total_bytes = 0;
  for (const auto& w : windows) {
    fastslide::RegionSpec region{
        .top_left = {w.x, w.y}, .size = {w.width, w.height}, .level = 0};
    auto result = reader.ReadRegion(region);
    if (!result.ok()) {
      return -1;
    }
    total_bytes += static_cast<int64_t>(w.width) * w.height * sizeof(uint32_t);
  }
  return total_bytes;
}

// Common driver: reads `windows` every iteration. When `capacity_bytes > 0` a
// per-reader LRU cache is attached, so the first iteration warms it and later
// iterations should hit.
void RunOverlapping(benchmark::State& state, size_t capacity_bytes) {
  ReaderContext ctx;
  if (!ctx.Open()) {
    state.SkipWithError(
        "Failed to open slide (set FASTSLIDE_BENCHMARK_FILE to an absolute "
        "path to a supported slide)");
    return;
  }

  std::shared_ptr<fastslide::runtime::ITileCache> cache;
  if (capacity_bytes > 0) {
    auto cache_or = fastslide::runtime::LRUTileCache::Create(capacity_bytes);
    if (!cache_or.ok()) {
      state.SkipWithError("Failed to create tile cache");
      return;
    }
    cache = std::move(cache_or.value());
    ctx.reader()->SetCache(cache);
  }

  const uint32_t window = static_cast<uint32_t>(state.range(0));
  const uint32_t step = std::max<uint32_t>(1, window / 4);
  const std::vector<Window> windows = BuildOverlappingWindows(
      ctx.level_width(), ctx.level_height(), ctx.tile_size(), window, step);

  int64_t total_bytes = 0;
  for (auto _ : state) {
    const int64_t bytes = ReadWindows(*ctx.reader(), windows);
    if (bytes < 0) {
      state.SkipWithError("Failed to read region");
      break;
    }
    total_bytes += bytes;
  }

  state.SetItemsProcessed(state.iterations() *
                          static_cast<int64_t>(windows.size()));
  state.SetBytesProcessed(total_bytes);
  state.counters["windows"] = static_cast<double>(windows.size());
  if (cache) {
    const auto stats = cache->GetStats();
    state.counters["hit_ratio"] = stats.hit_ratio;
    state.counters["hits"] = static_cast<double>(stats.hits);
    state.counters["misses"] = static_cast<double>(stats.misses);
  }
}

void BM_OverlappingReads_NoCache(benchmark::State& state) {
  RunOverlapping(state, 0);
}

void BM_OverlappingReads_WithCache(benchmark::State& state) {
  // 1 GiB is ample to hold the small overlapping footprint's native tiles.
  RunOverlapping(state, static_cast<size_t>(1) << 30);
}

BENCHMARK(BM_OverlappingReads_NoCache)
    ->Arg(256)
    ->Arg(512)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_OverlappingReads_WithCache)
    ->Arg(256)
    ->Arg(512)
    ->Unit(benchmark::kMicrosecond);

}  // namespace

BENCHMARK_MAIN();
