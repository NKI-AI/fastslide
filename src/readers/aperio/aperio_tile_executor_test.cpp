// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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
//
// Regression tests for the AperioTileExecutor's stateless metadata resolution.
//
// Background: an earlier revision of `AperioReader` cached a per-request
// `TiffStructureMetadata` snapshot in a `mutable` member that was written by
// `PrepareRequest` and read by `ExecutePlan`. Concurrent `ReadRegion` calls
// for different pyramid levels would clobber that snapshot mid-flight,
// producing tile_index/page mismatches that surfaced as out-of-range tile
// reads in `simpletiff` (e.g. asking for tile 6147 on a page that only has
// 56 tiles). The fix re-derives per-op geometry from the read-only
// `TiffIndex` using `op.source_id` directly. These tests assert that
// invariant holds even under concurrent access.

#include "fastslide/readers/aperio/aperio_tile_executor.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/aperio/aperio_exec_context.h"
#include "simpletiff/index.h"

namespace fastslide {
namespace {

/// Synthetic pyramid level (page index, image dims, tile dims).
struct PyramidPage {
  uint32_t image_width;
  uint32_t image_height;
  uint16_t tile_width;
  uint16_t tile_height;
  uint16_t samples_per_pixel;
};

/// Construct a minimal synthetic SVS-like `TiffIndex` for unit tests.
///
/// The index only populates the fields that
/// `AperioTileExecutor::ResolveAccessParams` reads (page geometry, storage
/// type, payload pools). It deliberately performs no I/O.
simpletiff::TiffIndex BuildSyntheticIndex(
    const std::vector<PyramidPage>& pages) {
  simpletiff::TiffIndex index;
  index.SetFormat(/*bigtiff=*/true, /*little_endian=*/true,
                  /*file_size=*/0);

  for (const auto& page : pages) {
    simpletiff::TilesRec tiles;
    tiles.tile_w = page.tile_width;
    tiles.tile_h = page.tile_height;
    tiles.tiles_x = (page.image_width + page.tile_width - 1) / page.tile_width;
    tiles.tiles_y =
        (page.image_height + page.tile_height - 1) / page.tile_height;
    const uint32_t payload_id = index.AddTiles(std::move(tiles));

    simpletiff::PageHeader header;
    header.width = page.image_width;
    header.height = page.image_height;
    header.samples_per_pixel = page.samples_per_pixel;
    header.bits_per_sample = 8;
    header.storage = simpletiff::Storage::kTiles;
    header.payload_id = payload_id;
    index.AddPage(std::move(header));
  }

  return index;
}

class AperioTileExecutorResolveTest : public ::testing::Test {
 protected:
  // Mimics a small Aperio pyramid: largest level first, then progressively
  // smaller. Tile/page sizes intentionally vary across levels so that mixing
  // them up (the original concurrency bug) would yield observably wrong
  // params.
  std::vector<PyramidPage> levels_ = {
      {/*image_w=*/50456, /*image_h=*/60051, /*tile_w=*/240, /*tile_h=*/240,
       /*spp=*/3},
      {/*image_w=*/12614, /*image_h=*/15012, /*tile_w=*/240, /*tile_h=*/240,
       /*spp=*/3},
      {/*image_w=*/3153, /*image_h=*/3753, /*tile_w=*/240, /*tile_h=*/240,
       /*spp=*/3},
      {/*image_w=*/1576, /*image_h=*/1876, /*tile_w=*/240, /*tile_h=*/240,
       /*spp=*/3},
  };
};

TEST_F(AperioTileExecutorResolveTest, ResolveMatchesPageGeometry) {
  simpletiff::TiffIndex index = BuildSyntheticIndex(levels_);
  AperioExecContext context(/*filename=*/"synthetic.svs",
                            /*cache=*/nullptr, index);

  for (uint32_t page_idx = 0; page_idx < levels_.size(); ++page_idx) {
    core::TileReadOp op;
    op.source_id = page_idx;

    auto params_or = AperioTileExecutor::ResolveAccessParams(op, context);
    ASSERT_TRUE(params_or.ok())
        << "ResolveAccessParams failed for page " << page_idx << ": "
        << params_or.status().ToString();

    const auto& params = *params_or;
    EXPECT_EQ(params.page, page_idx);
    EXPECT_EQ(params.tile_width, levels_[page_idx].tile_width);
    EXPECT_EQ(params.tile_height, levels_[page_idx].tile_height);
    EXPECT_EQ(params.samples_per_pixel, levels_[page_idx].samples_per_pixel);
    EXPECT_TRUE(params.is_tiled);
  }
}

TEST_F(AperioTileExecutorResolveTest, RejectsOutOfRangePage) {
  simpletiff::TiffIndex index = BuildSyntheticIndex(levels_);
  AperioExecContext context(/*filename=*/"synthetic.svs",
                            /*cache=*/nullptr, index);

  core::TileReadOp op;
  op.source_id = static_cast<uint32_t>(levels_.size() + 5);

  auto params_or = AperioTileExecutor::ResolveAccessParams(op, context);
  EXPECT_FALSE(params_or.ok());
}

// Regression test for the original bug: concurrent ReadRegion calls across
// different pyramid levels on the same reader used to clobber a shared
// `TiffStructureMetadata` cache between PrepareRequest and ExecutePlan,
// producing page/tile_index mismatches (e.g. tile 6147 dispatched to a page
// with 56 tiles). With the new stateless executor, every op resolves its
// own params from `op.source_id` against the read-only `TiffIndex`, so the
// resolved geometry must always match the per-op page regardless of how
// many threads are doing this at once.
TEST_F(AperioTileExecutorResolveTest, ConcurrentResolveIsRaceFree) {
  simpletiff::TiffIndex index = BuildSyntheticIndex(levels_);
  AperioExecContext context(/*filename=*/"synthetic.svs",
                            /*cache=*/nullptr, index);

  constexpr int kNumThreads = 16;
  constexpr int kIterationsPerThread = 5000;

  std::atomic<int> mismatch_count{0};
  std::atomic<int> error_count{0};

  std::vector<std::thread> workers;
  workers.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    workers.emplace_back([&, t]() {
      for (int i = 0; i < kIterationsPerThread; ++i) {
        // Cycle through all pyramid levels so threads heavily interleave on
        // different pages. Before the fix, this is exactly the access pattern
        // that triggered the page/tile_index mismatch.
        const uint32_t page_idx =
            static_cast<uint32_t>((t + i) % levels_.size());

        core::TileReadOp op;
        op.source_id = page_idx;

        auto params_or = AperioTileExecutor::ResolveAccessParams(op, context);
        if (!params_or.ok()) {
          error_count.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        const auto& params = *params_or;
        if (params.page != page_idx ||
            params.tile_width != levels_[page_idx].tile_width ||
            params.tile_height != levels_[page_idx].tile_height ||
            params.samples_per_pixel != levels_[page_idx].samples_per_pixel) {
          mismatch_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(mismatch_count.load(), 0)
      << "Concurrent ResolveAccessParams returned wrong page geometry; "
         "the executor is no longer stateless.";
  EXPECT_EQ(error_count.load(), 0)
      << "Concurrent ResolveAccessParams returned errors unexpectedly.";
}

}  // namespace
}  // namespace fastslide
