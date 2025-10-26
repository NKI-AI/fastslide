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
#include <vector>

#include "benchmark/benchmark.h"
#include "openslide/openslide.h"

namespace {

constexpr const char* kTestFile =
    "/Users/jonasteuwen/data/T82-06575 H2 HE.mrxs";

// Fixed seed for reproducible random access benchmarks
constexpr uint32_t kRandomSeed = 42;

/// @brief OpenSlide wrapper for benchmarking
class OpenSlideReader {
 public:
  explicit OpenSlideReader(const std::string& filename)
      : filename_(filename), slide_(nullptr) {}

  ~OpenSlideReader() {
    if (slide_) {
      openslide_close(slide_);
    }
  }

  bool Open() {
    slide_ = openslide_open(filename_.c_str());
    if (!slide_) {
      return false;
    }

    const char* error = openslide_get_error(slide_);
    if (error) {
      openslide_close(slide_);
      slide_ = nullptr;
      return false;
    }

    // Disable cache for pure read performance measurement
    openslide_cache_t* no_cache = openslide_cache_create(0);
    openslide_set_cache(slide_, no_cache);
    openslide_cache_release(no_cache);

    return true;
  }

  int GetLevelCount() const {
    return slide_ ? openslide_get_level_count(slide_) : 0;
  }

  void GetLevelDimensions(int level, int64_t* width, int64_t* height) const {
    if (slide_) {
      openslide_get_level_dimensions(slide_, level, width, height);
    }
  }

  double GetLevelDownsample(int level) const {
    return slide_ ? openslide_get_level_downsample(slide_, level) : 1.0;
  }

  bool ReadRegion(uint32_t* buffer, int64_t x, int64_t y, int level,
                  int64_t width, int64_t height) {
    if (!slide_) {
      return false;
    }

    openslide_read_region(slide_, buffer, x, y, level, width, height);

    const char* error = openslide_get_error(slide_);
    return error == nullptr;
  }

 private:
  std::string filename_;
  openslide_t* slide_;
};

/// @brief Benchmark fixture for OpenSlide sequential tile reading
class OpenSlideSequentialFixture : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State& state) override {
    if (!reader_) {
      reader_ = std::make_unique<OpenSlideReader>(kTestFile);
      init_success_ = reader_->Open();
      if (!init_success_) {
        return;
      }
    }

    tile_size_ = static_cast<int>(state.range(0));
    level_ = static_cast<int>(state.range(1));

    // Get level dimensions
    reader_->GetLevelDimensions(level_, &level_width_, &level_height_);

    // Calculate tile grid
    tiles_x_ = (level_width_ + tile_size_ - 1) / tile_size_;
    tiles_y_ = (level_height_ + tile_size_ - 1) / tile_size_;
    total_tiles_ = tiles_x_ * tiles_y_;

    // Allocate buffer (reuse across iterations)
    buffer_.resize(tile_size_ * tile_size_);
  }

  void TearDown(const ::benchmark::State& state) override { buffer_.clear(); }

 protected:
  std::unique_ptr<OpenSlideReader> reader_;
  std::vector<uint32_t> buffer_;
  bool init_success_{false};
  int tile_size_{256};
  int level_{0};
  int64_t level_width_{0};
  int64_t level_height_{0};
  int64_t tiles_x_{0};
  int64_t tiles_y_{0};
  int64_t total_tiles_{0};
};

/// @brief OpenSlide row-major tile reading benchmark
BENCHMARK_DEFINE_F(OpenSlideSequentialFixture, RowMajor)

(benchmark::State& state) {
  if (!init_success_) {
    state.SkipWithError("Failed to initialize OpenSlide reader");
    return;
  }

  const int64_t tiles_to_read = total_tiles_;

  int64_t tile_idx = 0;
  int64_t total_bytes = 0;

  for (auto _ : state) {
    // Calculate tile coordinates (row-major order)
    int64_t tile_y = tile_idx / tiles_x_;
    int64_t tile_x = tile_idx % tiles_x_;

    // Convert to pixel coordinates at this level
    int64_t x = tile_x * tile_size_;
    int64_t y = tile_y * tile_size_;

    // Calculate actual tile dimensions (handle edge tiles)
    int64_t tile_w =
        std::min(static_cast<int64_t>(tile_size_), level_width_ - x);
    int64_t tile_h =
        std::min(static_cast<int64_t>(tile_size_), level_height_ - y);

    // For OpenSlide, we need level-0 coordinates
    double downsample = reader_->GetLevelDownsample(level_);
    int64_t x0 = static_cast<int64_t>(x * downsample);
    int64_t y0 = static_cast<int64_t>(y * downsample);

    // Read the tile
    if (!reader_->ReadRegion(buffer_.data(), x0, y0, level_, tile_w, tile_h)) {
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

/// @brief OpenSlide column-major tile reading benchmark
BENCHMARK_DEFINE_F(OpenSlideSequentialFixture, ColumnMajor)

(benchmark::State& state) {
  if (!init_success_) {
    state.SkipWithError("Failed to initialize OpenSlide reader");
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
    int64_t x = tile_x * tile_size_;
    int64_t y = tile_y * tile_size_;

    // Calculate actual tile dimensions (handle edge tiles)
    int64_t tile_w =
        std::min(static_cast<int64_t>(tile_size_), level_width_ - x);
    int64_t tile_h =
        std::min(static_cast<int64_t>(tile_size_), level_height_ - y);

    // For OpenSlide, we need level-0 coordinates
    double downsample = reader_->GetLevelDownsample(level_);
    int64_t x0 = static_cast<int64_t>(x * downsample);
    int64_t y0 = static_cast<int64_t>(y * downsample);

    // Read the tile
    if (!reader_->ReadRegion(buffer_.data(), x0, y0, level_, tile_w, tile_h)) {
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

/// @brief OpenSlide random access tile reading benchmark
BENCHMARK_DEFINE_F(OpenSlideSequentialFixture, RandomAccess)

(benchmark::State& state) {
  if (!init_success_) {
    state.SkipWithError("Failed to initialize OpenSlide reader");
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
    int64_t x = tile_x * tile_size_;
    int64_t y = tile_y * tile_size_;

    // Calculate actual tile dimensions (handle edge tiles)
    int64_t tile_w =
        std::min(static_cast<int64_t>(tile_size_), level_width_ - x);
    int64_t tile_h =
        std::min(static_cast<int64_t>(tile_size_), level_height_ - y);

    // For OpenSlide, we need level-0 coordinates
    double downsample = reader_->GetLevelDownsample(level_);
    int64_t x0 = static_cast<int64_t>(x * downsample);
    int64_t y0 = static_cast<int64_t>(y * downsample);

    // Read the tile
    if (!reader_->ReadRegion(buffer_.data(), x0, y0, level_, tile_w, tile_h)) {
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
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({128, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({256, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({512, 0})
    ->Unit(benchmark::kMicrosecond);

// Row-major benchmarks - Level 1
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({128, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({256, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({512, 1})
    ->Unit(benchmark::kMicrosecond);

// Row-major benchmarks - Level 2
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({128, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({256, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({512, 2})
    ->Unit(benchmark::kMicrosecond);

// Row-major benchmarks - Level 8 (lowest)
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({128, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({256, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RowMajor)
    ->Args({512, 8})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 0
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({128, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({256, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({512, 0})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 1
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({128, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({256, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({512, 1})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 2
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({128, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({256, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({512, 2})
    ->Unit(benchmark::kMicrosecond);

// Column-major benchmarks - Level 8 (lowest)
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({128, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({256, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, ColumnMajor)
    ->Args({512, 8})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 0
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({128, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({256, 0})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({512, 0})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 1
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({128, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({256, 1})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({512, 1})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 2
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({128, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({256, 2})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({512, 2})
    ->Unit(benchmark::kMicrosecond);

// Random access benchmarks - Level 8 (lowest)
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({128, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({256, 8})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(OpenSlideSequentialFixture, RandomAccess)
    ->Args({512, 8})
    ->Unit(benchmark::kMicrosecond);

}  // namespace

BENCHMARK_MAIN();
