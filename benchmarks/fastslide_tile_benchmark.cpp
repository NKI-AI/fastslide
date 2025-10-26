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
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "benchmark/benchmark.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/slide_reader.h"

namespace {

constexpr const char* kTestFile =
    "/Users/jonasteuwen/data/T82-06575 H2 HE.mrxs";

// Fixed seed for reproducible random access benchmarks
constexpr uint32_t kRandomSeed = 42;

/// @brief FastSlide wrapper for benchmarking
class FastSlideReader {
 public:
  explicit FastSlideReader(const std::string& filename) : filename_(filename) {}

  bool Open() {
    auto reader_or =
        fastslide::runtime::GetGlobalRegistry().CreateReader(filename_);
    if (!reader_or.ok()) {
      return false;
    }
    reader_ = std::move(reader_or.value());
    return true;
  }

  int GetLevelCount() const { return reader_ ? reader_->GetLevelCount() : 0; }

  absl::StatusOr<fastslide::LevelInfo> GetLevelInfo(int level) const {
    if (!reader_) {
      return absl::InternalError("Reader not initialized");
    }
    return reader_->GetLevelInfo(level);
  }

  absl::StatusOr<fastslide::Image> ReadRegion(uint32_t x, uint32_t y, int level,
                                              uint32_t width, uint32_t height) {
    if (!reader_) {
      return absl::InternalError("Reader not initialized");
    }

    fastslide::RegionSpec region{
        .top_left = {x, y}, .size = {width, height}, .level = level};

    return reader_->ReadRegion(region);
  }

 private:
  std::string filename_;
  std::unique_ptr<fastslide::SlideReader> reader_;
};

/// @brief Benchmark fixture for FastSlide sequential tile reading
class FastSlideSequentialFixture : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State& state) override {
    if (!reader_) {
      reader_ = std::make_unique<FastSlideReader>(kTestFile);
      init_success_ = reader_->Open();
      if (!init_success_) {
        return;
      }
    }

    tile_size_ = static_cast<int>(state.range(0));
    level_ = static_cast<int>(state.range(1));

    // Get level dimensions
    auto level_info_or = reader_->GetLevelInfo(level_);
    if (!level_info_or.ok()) {
      init_success_ = false;
      return;
    }

    auto level_info = level_info_or.value();
    level_width_ = level_info.dimensions[0];
    level_height_ = level_info.dimensions[1];

    // Calculate tile grid
    tiles_x_ = (level_width_ + tile_size_ - 1) / tile_size_;
    tiles_y_ = (level_height_ + tile_size_ - 1) / tile_size_;
    total_tiles_ = tiles_x_ * tiles_y_;
  }

  void TearDown(const ::benchmark::State& state) override {}

 protected:
  std::unique_ptr<FastSlideReader> reader_;
  bool init_success_{false};
  int tile_size_{256};
  int level_{0};
  uint32_t level_width_{0};
  uint32_t level_height_{0};
  int64_t tiles_x_{0};
  int64_t tiles_y_{0};
  int64_t total_tiles_{0};
};

/// @brief FastSlide row-major tile reading benchmark
BENCHMARK_DEFINE_F(FastSlideSequentialFixture, RowMajor)

(benchmark::State& state) {
  if (!init_success_) {
    state.SkipWithError("Failed to initialize FastSlide reader");
    return;
  }

  const int64_t tiles_to_read = total_tiles_;

  int64_t tile_idx = 0;
  int64_t total_bytes = 0;

  for (auto _ : state) {
    // Calculate tile coordinates (row-major order: left→right, then down)
    int64_t tile_y = tile_idx / tiles_x_;
    int64_t tile_x = tile_idx % tiles_x_;

    // Convert to pixel coordinates at this level
    uint32_t x = static_cast<uint32_t>(tile_x * tile_size_);
    uint32_t y = static_cast<uint32_t>(tile_y * tile_size_);

    // Calculate actual tile dimensions (handle edge tiles)
    uint32_t tile_w =
        std::min(static_cast<uint32_t>(tile_size_), level_width_ - x);
    uint32_t tile_h =
        std::min(static_cast<uint32_t>(tile_size_), level_height_ - y);

    // Read the tile (FastSlide uses native level coordinates)
    auto result = reader_->ReadRegion(x, y, level_, tile_w, tile_h);
    if (!result.ok()) {
      state.SkipWithError("Failed to read region");
      break;
    }

    // Track bytes processed (RGBA pixels are 4 bytes each)
    total_bytes += tile_w * tile_h * sizeof(uint32_t);

    // Move to next tile
    tile_idx = (tile_idx + 1) % tiles_to_read;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(total_bytes);
}

/// @brief FastSlide column-major tile reading benchmark
BENCHMARK_DEFINE_F(FastSlideSequentialFixture, ColumnMajor)

(benchmark::State& state) {
  if (!init_success_) {
    state.SkipWithError("Failed to initialize FastSlide reader");
    return;
  }

  const int64_t tiles_to_read = total_tiles_;

  int64_t tile_idx = 0;
  int64_t total_bytes = 0;

  for (auto _ : state) {
    // Calculate tile coordinates (column-major order: top→bottom, then right)
    int64_t tile_x = tile_idx / tiles_y_;
    int64_t tile_y = tile_idx % tiles_y_;

    // Convert to pixel coordinates at this level
    uint32_t x = static_cast<uint32_t>(tile_x * tile_size_);
    uint32_t y = static_cast<uint32_t>(tile_y * tile_size_);

    // Calculate actual tile dimensions (handle edge tiles)
    uint32_t tile_w =
        std::min(static_cast<uint32_t>(tile_size_), level_width_ - x);
    uint32_t tile_h =
        std::min(static_cast<uint32_t>(tile_size_), level_height_ - y);

    // Read the tile (FastSlide uses native level coordinates)
    auto result = reader_->ReadRegion(x, y, level_, tile_w, tile_h);
    if (!result.ok()) {
      state.SkipWithError("Failed to read region");
      break;
    }

    // Track bytes processed (RGBA pixels are 4 bytes each)
    total_bytes += tile_w * tile_h * sizeof(uint32_t);

    // Move to next tile
    tile_idx = (tile_idx + 1) % tiles_to_read;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(total_bytes);
}

/// @brief FastSlide random access tile reading benchmark
BENCHMARK_DEFINE_F(FastSlideSequentialFixture, RandomAccess)

(benchmark::State& state) {
  if (!init_success_) {
    state.SkipWithError("Failed to initialize FastSlide reader");
    return;
  }

  // Generate random tile indices (at least 10% of total tiles)
  const int64_t tiles_to_read =
      std::max(static_cast<int64_t>(1), total_tiles_ / 10);

  std::mt19937 rng(kRandomSeed);
  std::uniform_int_distribution<int64_t> dist(0, total_tiles_ - 1);

  std::vector<int64_t> tile_indices;
  tile_indices.reserve(tiles_to_read);
  for (int64_t i = 0; i < tiles_to_read; ++i) {
    tile_indices.push_back(dist(rng));
  }

  int64_t access_idx = 0;
  int64_t total_bytes = 0;

  for (auto _ : state) {
    int64_t tile_idx = tile_indices[access_idx];

    // Calculate tile coordinates (row-major order)
    int64_t tile_y = tile_idx / tiles_x_;
    int64_t tile_x = tile_idx % tiles_x_;

    // Convert to pixel coordinates at this level
    uint32_t x = static_cast<uint32_t>(tile_x * tile_size_);
    uint32_t y = static_cast<uint32_t>(tile_y * tile_size_);

    // Calculate actual tile dimensions (handle edge tiles)
    uint32_t tile_w =
        std::min(static_cast<uint32_t>(tile_size_), level_width_ - x);
    uint32_t tile_h =
        std::min(static_cast<uint32_t>(tile_size_), level_height_ - y);

    // Read the tile (FastSlide uses native level coordinates)
    auto result = reader_->ReadRegion(x, y, level_, tile_w, tile_h);
    if (!result.ok()) {
      state.SkipWithError("Failed to read region");
      break;
    }

    // Track bytes processed (RGBA pixels are 4 bytes each)
    total_bytes += tile_w * tile_h * sizeof(uint32_t);

    // Move to next random tile
    access_idx = (access_idx + 1) % tiles_to_read;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(total_bytes);
}

// Register benchmarks with different tile sizes and levels
// Format: ->Args({tile_size, level})

// Row-major benchmarks - Level 0
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({128, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({256, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({512, 0})
    ->Unit(benchmark::kMicrosecond);

// Row-major benchmarks - Level 1
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({128, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({256, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({512, 1})
    ->Unit(benchmark::kMicrosecond);

// Row-major benchmarks - Level 2
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({128, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({256, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({512, 2})
    ->Unit(benchmark::kMicrosecond);

// Row-major benchmarks - Level 8 (lowest)
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({128, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({256, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RowMajor)
    ->Args({512, 8})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 0
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({128, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({256, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({512, 0})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 1
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({128, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({256, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({512, 1})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 2
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({128, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({256, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({512, 2})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 8 (lowest)
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({128, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({256, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, ColumnMajor)
    ->Args({512, 8})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 0
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({128, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({256, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({512, 0})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 1
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({128, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({256, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({512, 1})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 2
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({128, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({256, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({512, 2})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 8 (lowest)
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({128, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({256, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(FastSlideSequentialFixture, RandomAccess)
    ->Args({512, 8})
    ->Unit(benchmark::kMicrosecond);

}  // namespace

BENCHMARK_MAIN();
