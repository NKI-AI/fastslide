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

#include "fastslide/readers/read_parallelism.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace fastslide {
namespace readers {
namespace {

class ReadParallelismTest : public ::testing::Test {
 protected:
  void TearDown() override { SetReadParallelism(ReadParallelism::kThreadPool); }
};

TEST_F(ReadParallelismTest, DefaultsToThreadPool) {
  EXPECT_EQ(GetReadParallelism(), ReadParallelism::kThreadPool);
}

TEST_F(ReadParallelismTest, SetAndGet) {
  SetReadParallelism(ReadParallelism::kCallingThread);
  EXPECT_EQ(GetReadParallelism(), ReadParallelism::kCallingThread);
  SetReadParallelism(ReadParallelism::kThreadPool);
  EXPECT_EQ(GetReadParallelism(), ReadParallelism::kThreadPool);
}

TEST_F(ReadParallelismTest, RunsEveryIndexOnceInBothModes) {
  for (const auto mode :
       {ReadParallelism::kThreadPool, ReadParallelism::kCallingThread}) {
    SetReadParallelism(mode);
    constexpr std::size_t kCount = 257;
    std::vector<std::atomic<int>> hits(kCount);
    ForEachTileOp(kCount, [&](std::size_t i) {
      hits[i].fetch_add(1, std::memory_order_relaxed);
    });
    for (std::size_t i = 0; i < kCount; ++i) {
      EXPECT_EQ(hits[i].load(), 1) << "index " << i;
    }
  }
}

TEST_F(ReadParallelismTest, CallingThreadModeStaysOnTheCaller) {
  SetReadParallelism(ReadParallelism::kCallingThread);
  const auto caller = std::this_thread::get_id();
  std::atomic<int> elsewhere{0};
  ForEachTileOp(64, [&](std::size_t) {
    if (std::this_thread::get_id() != caller) {
      elsewhere.fetch_add(1, std::memory_order_relaxed);
    }
  });
  EXPECT_EQ(elsewhere.load(), 0);
}

TEST_F(ReadParallelismTest, SingleOpStaysOnTheCallerInThreadPoolMode) {
  const auto caller = std::this_thread::get_id();
  std::thread::id ran_on;
  ForEachTileOp(1, [&](std::size_t) { ran_on = std::this_thread::get_id(); });
  EXPECT_EQ(ran_on, caller);
}

TEST_F(ReadParallelismTest, ZeroOpsIsANoOp) {
  int calls = 0;
  ForEachTileOp(0, [&](std::size_t) { ++calls; });
  EXPECT_EQ(calls, 0);
}

}  // namespace
}  // namespace readers
}  // namespace fastslide
