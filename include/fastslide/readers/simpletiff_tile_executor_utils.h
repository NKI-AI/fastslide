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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_TILE_EXECUTOR_UTILS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_TILE_EXECUTOR_UTILS_H_

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>

#include "aifocore/status/result.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {
namespace readers {
namespace simpletiff_exec {

enum class ErrorPolicy : std::uint8_t {
  kStopOnFirstError,
  kBestEffort,
};

// Helper for writing a tile from parallel workers:
// - Direct writers can write disjoint regions without locking.
// - Blended writers accumulate into shared buffers and must be protected.
inline aifocore::Status WriteTileMaybeLocked(
    runtime::TileWriter &writer, const core::TileReadOp &operation,
    std::span<const uint8_t> pixel_data, uint32_t tile_width,
    uint32_t tile_height, uint32_t tile_channels, std::mutex &writer_mutex) {
  if (writer.IsBlendingEnabled()) {
    return writer.WriteTile(operation, pixel_data, tile_width, tile_height,
                            tile_channels, writer_mutex);
  }
  return writer.WriteTile(operation, pixel_data, tile_width, tile_height,
                          tile_channels);
}

// Executes `plan.operations` in parallel using the global ThreadPoolManager.
//
// - Uses `writer_mutex` when calling into TileWriter to support blended writers
//   safely; direct writers ignore the mutex.
//
// `execute_one(op, writer, writer_mutex)` should be thread-safe and must not
// throw exceptions.
//
// Error handling is controlled by `policy`:
// - kStopOnFirstError: returns the first error status (best-effort).
// - kBestEffort: runs all operations and always returns OK.
template <typename ExecuteOneFn, typename OnErrorFn>
aifocore::Status
ExecuteOpsWithThreadPool(const core::TilePlan &plan,
                         runtime::TileWriter &writer, ExecuteOneFn execute_one,
                         ErrorPolicy policy, OnErrorFn on_error) {
  if (plan.operations.empty()) {
    const auto &background = plan.output.background;
    return writer.FillBackground(background.r, background.g, background.b);
  }

  auto &pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex writer_mutex;
  std::mutex error_mutex;
  std::optional<aifocore::Status> first_error;
  std::atomic<bool> has_error{false};
  std::atomic<int> error_count{0};

  auto futures =
      pool.submit_sequence(0, plan.operations.size(), [&](size_t index) {
        if (policy == ErrorPolicy::kStopOnFirstError &&
            has_error.load(std::memory_order_relaxed)) {
          return;
        }
        const auto &operation = plan.operations[index];
        aifocore::Status status = execute_one(operation, writer, writer_mutex);
        if (!status.ok()) {
          error_count.fetch_add(1, std::memory_order_relaxed);
          on_error(operation, status);
          if (policy == ErrorPolicy::kStopOnFirstError) {
            has_error.store(true, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(error_mutex);
            if (!first_error.has_value()) {
              first_error = status;
            }
          }
        }
      });

  futures.wait();
  if (policy == ErrorPolicy::kStopOnFirstError && first_error.has_value()) {
    return *first_error;
  }
  return aifocore::Status::OkStatus();
}

template <typename ExecuteOneFn>
aifocore::Status
ExecuteOpsWithThreadPoolStopOnError(const core::TilePlan &plan,
                                    runtime::TileWriter &writer,
                                    ExecuteOneFn execute_one) {
  return ExecuteOpsWithThreadPool(
      plan, writer, execute_one, ErrorPolicy::kStopOnFirstError,
      [](const core::TileReadOp &, const aifocore::Status &) {});
}

template <typename ExecuteOneFn, typename OnErrorFn>
aifocore::Status ExecuteOpsWithThreadPoolBestEffort(const core::TilePlan &plan,
                                                    runtime::TileWriter &writer,
                                                    ExecuteOneFn execute_one,
                                                    OnErrorFn on_error) {
  return ExecuteOpsWithThreadPool(plan, writer, execute_one,
                                  ErrorPolicy::kBestEffort, on_error);
}

} // namespace simpletiff_exec
} // namespace readers
} // namespace fastslide

#endif // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_TILE_EXECUTOR_UTILS_H_
