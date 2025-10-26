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

#include "aifocore/utilities/thread_pool_singleton.h"

#include <cstddef>
#include <cstdlib>

#include "aifocore/utilities/bs_thread_pool.h"

namespace aifocore {

namespace {

/// @brief Get thread count from environment variable or default
///
/// Checks NUM_THREADS environment variable (OpenMP-style):
/// - If not set: returns 0 (use hardware_concurrency)
/// - If set to valid positive number: returns that value
/// - If set to 0 or 1: returns 1 (single-threaded/no parallelism)
/// - If invalid: returns 0 (use hardware_concurrency)
///
/// @return Thread count to use (0 = hardware_concurrency)
std::size_t GetThreadCountFromEnv() {
  const char* env_value = std::getenv("NUM_THREADS");
  if (env_value == nullptr) {
    return 0;  // Not set, use default
  }

  // Try to parse as integer
  char* end = nullptr;
  const long value = std::strtol(env_value, &end, 10);

  // Check if parsing was successful and value is reasonable
  if (end == env_value || *end != '\0' || value < 0) {
    // Invalid value, use default
    return 0;
  }

  // Return the parsed value (0 will be interpreted as hardware_concurrency by BS::thread_pool)
  return static_cast<std::size_t>(value);
}

}  // namespace

BS::light_thread_pool& ThreadPoolManager::GetInstance() {
  return GetPool(0);
}

void ThreadPoolManager::SetThreadCount(std::size_t count) {
  GetPool(count).reset(count);
}

BS::light_thread_pool& ThreadPoolManager::GetPool(std::size_t count) {
  // Thread-safe static initialization (C++11 guaranteed)
  // On first call, check environment variable for thread count override
  static const std::size_t env_thread_count = GetThreadCountFromEnv();
  static BS::light_thread_pool pool(count == 0 ? env_thread_count : count);
  return pool;
}

}  // namespace aifocore
