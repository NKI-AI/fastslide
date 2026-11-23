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

#include "fastslide/threading.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "aifocore/utilities/thread_pool_singleton.h"

namespace fastslide {

namespace {

// One definition for the whole process (not an inline static in a header,
// which shared libraries built with hidden visibility would each copy).
std::atomic<ReadParallelism>& ReadParallelismSetting() {
  static std::atomic<ReadParallelism> setting{[] {
    const char* env = std::getenv("FASTSLIDE_READ_PARALLELISM");
    if (env != nullptr && std::strcmp(env, "calling_thread") == 0) {
      return ReadParallelism::kCallingThread;
    }
    return ReadParallelism::kThreadPool;
  }()};
  return setting;
}

}  // namespace

void SetReadParallelism(ReadParallelism mode) {
  ReadParallelismSetting().store(mode, std::memory_order_relaxed);
}

ReadParallelism GetReadParallelism() {
  return ReadParallelismSetting().load(std::memory_order_relaxed);
}

void SetThreadPoolSize(std::size_t num_threads) {
  aifocore::ThreadPoolManager::SetThreadCount(num_threads);
}

std::size_t GetThreadPoolSize() {
  return aifocore::ThreadPoolManager::GetInstance().get_thread_count();
}

}  // namespace fastslide
