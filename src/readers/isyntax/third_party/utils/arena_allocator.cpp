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

#include "fastslide/readers/isyntax/third_party/utils/arena_allocator.h"

#include <cstdlib>
#include <new>

namespace isyntax {

// Thread-local storage for the arena
// Default size: 256MB per thread (same as C implementation)
constexpr size_t kDefaultArenaSize = 256 * 1024 * 1024;  // 256 MB

aifocore::Result<Arena*> ThreadLocalArena::Get() {
  // The 'thread_local' keyword ensures each thread gets its own Arena instance.
  // This is initialized lazily on first access per thread.
  // Thread safety: Each thread has its own Arena, so no synchronization needed.
  thread_local Arena arena;
  thread_local bool initialized = false;
  thread_local aifocore::Status init_status = aifocore::Status::OkStatus();

  if (!initialized) {
    init_status = arena.Init(kDefaultArenaSize);
    initialized = true;
  }
  if (!init_status.ok()) {
    return init_status;
  }
  return &arena;
}

}  // namespace isyntax
