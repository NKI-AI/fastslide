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

#include <benchmark/benchmark.h>
#include <tiffio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#include "simpletiff/index.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_parser.h"

namespace {

// Get benchmark file path from environment variable
// Usage: SIMPLETIFF_BENCHMARK_FILENAME=/path/to/file.svs bazelisk run
// //aifo/simpletiff/benchmarks:bench_tile_reading
const char *GetBenchmarkFilePath() {
  static const char *cached_path = nullptr;
  if (cached_path == nullptr) {
    const char *env_path = std::getenv("SIMPLETIFF_BENCHMARK_FILE");
    if (env_path == nullptr) {
      return nullptr; // Error: environment variable not set
    }
    cached_path = env_path;
  }
  return cached_path;
}

// -----------------------------------------------------------
// SimpleTIFF Benchmarks
// -----------------------------------------------------------

static void BM_SimpleTIFF_SequentialTiles(benchmark::State &state) {
  const char *filename = GetBenchmarkFilePath();
  if (filename == nullptr) {
    state.SkipWithError(
        "SIMPLETIFF_BENCHMARK_FILENAME environment variable not set");
    return;
  }

  // Open and parse the TIFF
  simpletiff::TiffIndex index;
  int fd = -1;

  if (!simpletiff::OpenTiff(filename, index, fd)) {
    state.SkipWithError("Could not open or parse TIFF file");
    return;
  }

  if (index.NumPages() == 0 ||
      index.Page(0).storage != simpletiff::Storage::kTiles) {
    ::close(fd);
    state.SkipWithError("First page is not tiled");
    return;
  }

  const auto &page = index.Page(0);
  const auto &tiles = index.Tiles(page.payload_id);
  const uint32_t total_tiles = tiles.tiles_x * tiles.tiles_y;

  // Reusable output buffer (thread_local buffers used internally)
  std::vector<uint8_t> tile_data;
  simpletiff::DecodeContext ctx;

  int64_t tiles_read = 0;
  int64_t bytes_read = 0;

  for (auto _ : state) {
    // Read all tiles sequentially by index
    for (uint32_t tile_idx = 0; tile_idx < total_tiles; ++tile_idx) {
      int tile_w = 0, tile_h = 0;
      auto result = simpletiff::ReadTile(index, 0, tile_idx, ctx, tile_data,
                                         tile_w, tile_h);
      if (result) {
        bytes_read += tile_data.size();
        tiles_read++;
        benchmark::DoNotOptimize(tile_data.data());
      }
    }
  }

  ::close(fd);

  state.SetItemsProcessed(tiles_read);
  state.SetBytesProcessed(bytes_read);
  state.counters["tiles"] = benchmark::Counter(static_cast<double>(tiles_read),
                                               benchmark::Counter::kIsRate);
}

static void BM_SimpleTIFF_RandomTiles(benchmark::State &state) {
  const char *filename = GetBenchmarkFilePath();
  if (filename == nullptr) {
    state.SkipWithError(
        "SIMPLETIFF_BENCHMARK_FILENAME environment variable not set");
    return;
  }

  // Open and parse the TIFF
  simpletiff::TiffIndex index;
  int fd = -1;

  if (!simpletiff::OpenTiff(filename, index, fd)) {
    state.SkipWithError("Could not open or parse TIFF file");
    return;
  }

  if (index.NumPages() == 0 ||
      index.Page(0).storage != simpletiff::Storage::kTiles) {
    ::close(fd);
    state.SkipWithError("First page is not tiled");
    return;
  }

  const auto &page = index.Page(0);
  const auto &tiles = index.Tiles(page.payload_id);
  const uint32_t total_tiles = tiles.tiles_x * tiles.tiles_y;

  // Generate random tile order (fixed seed for reproducibility)
  std::vector<uint32_t> tile_order(total_tiles);
  for (uint32_t i = 0; i < total_tiles; ++i) {
    tile_order[i] = i;
  }

  std::mt19937 rng(42); // Fixed seed
  std::shuffle(tile_order.begin(), tile_order.end(), rng);

  // Reusable output buffer (thread_local buffers used internally)
  std::vector<uint8_t> tile_data;
  simpletiff::DecodeContext ctx;

  int64_t tiles_read = 0;
  int64_t bytes_read = 0;

  for (auto _ : state) {
    // Read tiles in random order by index
    for (uint32_t tile_idx : tile_order) {
      int tile_w = 0, tile_h = 0;
      auto result = simpletiff::ReadTile(index, 0, tile_idx, ctx, tile_data,
                                         tile_w, tile_h);
      if (result) {
        bytes_read += tile_data.size();
        tiles_read++;
        benchmark::DoNotOptimize(tile_data.data());
      }
    }
  }

  ::close(fd);

  state.SetItemsProcessed(tiles_read);
  state.SetBytesProcessed(bytes_read);
  state.counters["tiles"] = benchmark::Counter(static_cast<double>(tiles_read),
                                               benchmark::Counter::kIsRate);
}

// -----------------------------------------------------------
// libtiff Benchmarks
// -----------------------------------------------------------

static void BM_LibTIFF_SequentialTiles(benchmark::State &state) {
  const char *filename = GetBenchmarkFilePath();
  if (filename == nullptr) {
    state.SkipWithError(
        "SIMPLETIFF_BENCHMARK_FILENAME environment variable not set");
    return;
  }

  TIFF *tif = TIFFOpen(filename, "r");
  if (!tif) {
    state.SkipWithError("Could not open test file");
    return;
  }

  int tile_count = TIFFNumberOfTiles(tif);
  uint32_t tile_size = TIFFTileSize(tif);

  std::vector<uint8_t> tile_buffer(tile_size);

  int64_t tiles_read = 0;
  int64_t bytes_read = 0;

  for (auto _ : state) {
    TIFFSetDirectory(tif, 0);

    // Read all tiles sequentially
    for (int tile_idx = 0; tile_idx < tile_count; ++tile_idx) {
      tsize_t bytes =
          TIFFReadEncodedTile(tif, tile_idx, tile_buffer.data(), tile_size);
      if (bytes > 0) {
        bytes_read += bytes;
        tiles_read++;
        benchmark::DoNotOptimize(tile_buffer.data());
      }
    }
  }

  TIFFClose(tif);

  state.SetItemsProcessed(tiles_read);
  state.SetBytesProcessed(bytes_read);
  state.counters["tiles"] = benchmark::Counter(static_cast<double>(tiles_read),
                                               benchmark::Counter::kIsRate);
}

static void BM_LibTIFF_RandomTiles(benchmark::State &state) {
  const char *filename = GetBenchmarkFilePath();
  if (filename == nullptr) {
    state.SkipWithError(
        "SIMPLETIFF_BENCHMARK_FILENAME environment variable not set");
    return;
  }

  TIFF *tif = TIFFOpen(filename, "r");
  if (!tif) {
    state.SkipWithError("Could not open test file");
    return;
  }

  int tile_count = TIFFNumberOfTiles(tif);
  uint32_t tile_size = TIFFTileSize(tif);

  // Generate random tile order (fixed seed for reproducibility)
  std::vector<int> tile_order(tile_count);
  for (int i = 0; i < tile_count; ++i) {
    tile_order[i] = i;
  }

  std::mt19937 rng(42); // Fixed seed - same as SimpleTIFF benchmark
  std::shuffle(tile_order.begin(), tile_order.end(), rng);

  std::vector<uint8_t> tile_buffer(tile_size);

  int64_t tiles_read = 0;
  int64_t bytes_read = 0;

  for (auto _ : state) {
    TIFFSetDirectory(tif, 0);

    // Read tiles in random order
    for (int tile_idx : tile_order) {
      tsize_t bytes =
          TIFFReadEncodedTile(tif, tile_idx, tile_buffer.data(), tile_size);
      if (bytes > 0) {
        bytes_read += bytes;
        tiles_read++;
        benchmark::DoNotOptimize(tile_buffer.data());
      }
    }
  }

  TIFFClose(tif);

  state.SetItemsProcessed(tiles_read);
  state.SetBytesProcessed(bytes_read);
  state.counters["tiles"] = benchmark::Counter(static_cast<double>(tiles_read),
                                               benchmark::Counter::kIsRate);
}

// -----------------------------------------------------------
// Random Tiles with File Open/Close per Tile
// -----------------------------------------------------------

static void BM_SimpleTIFF_RandomTiles_OpenClose(benchmark::State &state) {
  const char *filename = GetBenchmarkFilePath();
  if (filename == nullptr) {
    state.SkipWithError(
        "SIMPLETIFF_BENCHMARK_FILENAME environment variable not set");
    return;
  }

  // Parse once to get tile information
  simpletiff::TiffIndex index_template;
  int fd_temp = -1;

  if (!simpletiff::OpenTiff(filename, index_template, fd_temp)) {
    state.SkipWithError("Could not open or parse TIFF file");
    return;
  }

  if (index_template.NumPages() == 0 ||
      index_template.Page(0).storage != simpletiff::Storage::kTiles) {
    ::close(fd_temp);
    state.SkipWithError("First page is not tiled");
    return;
  }

  const auto &page = index_template.Page(0);
  const auto &tiles = index_template.Tiles(page.payload_id);
  const uint32_t total_tiles = tiles.tiles_x * tiles.tiles_y;
  ::close(fd_temp);

  // Generate random tile order (fixed seed for reproducibility)
  std::vector<uint32_t> tile_order(total_tiles);
  for (uint32_t i = 0; i < total_tiles; ++i) {
    tile_order[i] = i;
  }

  std::mt19937 rng(42); // Fixed seed
  std::shuffle(tile_order.begin(), tile_order.end(), rng);

  std::vector<uint8_t> tile_data;

  int64_t tiles_read = 0;
  int64_t bytes_read = 0;

  for (auto _ : state) {
    // Read tiles in random order, opening/closing file each time
    for (uint32_t tile_idx : tile_order) {
      // Open and parse for each tile (lazy loading now makes this fast!)
      simpletiff::TiffIndex index;
      int fd = -1;
      simpletiff::DecodeContext ctx;

      if (simpletiff::OpenTiff(filename, index, fd)) {
        int tile_w = 0, tile_h = 0;
        auto result = simpletiff::ReadTile(index, 0, tile_idx, ctx, tile_data,
                                           tile_w, tile_h);
        if (result) {
          bytes_read += tile_data.size();
          tiles_read++;
          benchmark::DoNotOptimize(tile_data.data());
        }
        ::close(fd);
      }
    }
  }

  state.SetItemsProcessed(tiles_read);
  state.SetBytesProcessed(bytes_read);
  state.counters["tiles"] = benchmark::Counter(static_cast<double>(tiles_read),
                                               benchmark::Counter::kIsRate);
}

static void BM_LibTIFF_RandomTiles_OpenClose(benchmark::State &state) {
  const char *filename = GetBenchmarkFilePath();
  if (filename == nullptr) {
    state.SkipWithError(
        "SIMPLETIFF_BENCHMARK_FILENAME environment variable not set");
    return;
  }

  // Get tile count
  TIFF *tif_temp = TIFFOpen(filename, "r");
  if (!tif_temp) {
    state.SkipWithError("Could not open test file");
    return;
  }

  int tile_count = TIFFNumberOfTiles(tif_temp);
  uint32_t tile_size = TIFFTileSize(tif_temp);
  TIFFClose(tif_temp);

  // Generate random tile order (fixed seed for reproducibility)
  std::vector<int> tile_order(tile_count);
  for (int i = 0; i < tile_count; ++i) {
    tile_order[i] = i;
  }

  std::mt19937 rng(42); // Fixed seed - same as SimpleTIFF benchmark
  std::shuffle(tile_order.begin(), tile_order.end(), rng);

  std::vector<uint8_t> tile_buffer(tile_size);

  int64_t tiles_read = 0;
  int64_t bytes_read = 0;

  for (auto _ : state) {
    // Read tiles in random order, opening/closing file each time
    for (int tile_idx : tile_order) {
      TIFF *tif = TIFFOpen(filename, "r");
      if (tif) {
        tsize_t bytes =
            TIFFReadEncodedTile(tif, tile_idx, tile_buffer.data(), tile_size);
        if (bytes > 0) {
          bytes_read += bytes;
          tiles_read++;
          benchmark::DoNotOptimize(tile_buffer.data());
        }
        TIFFClose(tif);
      }
    }
  }

  state.SetItemsProcessed(tiles_read);
  state.SetBytesProcessed(bytes_read);
  state.counters["tiles"] = benchmark::Counter(static_cast<double>(tiles_read),
                                               benchmark::Counter::kIsRate);
}

// Register benchmarks
BENCHMARK(BM_SimpleTIFF_SequentialTiles)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SimpleTIFF_RandomTiles)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SimpleTIFF_RandomTiles_OpenClose)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_LibTIFF_SequentialTiles)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_LibTIFF_RandomTiles)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_LibTIFF_RandomTiles_OpenClose)->Unit(benchmark::kMillisecond);

} // namespace

BENCHMARK_MAIN();
