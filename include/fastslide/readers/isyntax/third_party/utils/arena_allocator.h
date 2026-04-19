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

/// @file arena_allocator.h
/// @brief Pure C++ arena (linear/bump) allocator
///
/// Complete C++ rewrite replacing the C arena allocator, providing:
/// - RAII for automatic cleanup
/// - Type-safe allocations via templates
/// - Move semantics for ownership transfer
/// - Thread-local storage
/// - Exception-safe resource management

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>

#include "../platform/common.h"
#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace isyntax {

/// @brief Fast linear (bump) allocator for temporary allocations
///
/// A simple arena allocator that provides very fast allocation by simply
/// incrementing a pointer. All memory is freed at once when the arena is
/// destroyed or when a scoped region is released.
///
/// Thread-safety: Not thread-safe. Each thread should have its own Arena.
class Arena {
 public:
  static constexpr size_t kAlignment = 32;

  /// @brief Constructs an empty arena (call `Init()` before use).
  Arena() = default;

  /// @brief Initialize an arena with the given capacity.
  ///
  /// This never aborts; allocation failures are returned as a Status.
  aifocore::Status Init(size_t capacity) {
    if (capacity == 0) {
      return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                              "Arena::Init: capacity must be > 0");
    }
    if (base_ != nullptr) {
      return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                              "Arena::Init: already initialized");
    }

    // Ensure size is a multiple of alignment.
    const size_t rounded_capacity =
        (capacity + (kAlignment - 1)) & ~(kAlignment - 1);

    // `std::aligned_alloc` isn't available on all platforms/toolchains (notably
    // some Windows libc implementations). Use aligned `operator new` instead.
    base_ = static_cast<uint8_t*>(::operator new(
        rounded_capacity, std::align_val_t(kAlignment), std::nothrow));
    if (base_ == nullptr) {
      return aifocore::Status(
          aifocore::StatusCode::kResourceExhausted,
          aifocore::fmt::format("Arena::Init: allocation failed (capacity={})",
                                rounded_capacity));
    }

    capacity_ = rounded_capacity;
    used_ = 0;
    temp_count_ = 0;
    return aifocore::Status::OkStatus();
  }

  ~Arena() { Free(); }

  // Delete copy operations
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  // Move operations
  Arena(Arena&& other) noexcept
      : base_(other.base_),
        capacity_(other.capacity_),
        used_(other.used_),
        temp_count_(other.temp_count_) {
    other.base_ = nullptr;
    other.capacity_ = 0;
    other.used_ = 0;
    other.temp_count_ = 0;
  }

  Arena& operator=(Arena&& other) noexcept {
    if (this != &other) {
      Free();
      base_ = other.base_;
      capacity_ = other.capacity_;
      used_ = other.used_;
      temp_count_ = other.temp_count_;
      other.base_ = nullptr;
      other.capacity_ = 0;
      other.used_ = 0;
      other.temp_count_ = 0;
    }
    return *this;
  }

  /// @brief Allocate size bytes from the arena
  /// @param size Number of bytes to allocate
  /// @return Pointer to allocated memory
  void* Allocate(size_t size) {
    ASSERT(used_ + size <= capacity_);
    void* result = base_ + used_;
    used_ += size;
    return result;
  }

  /// @brief Allocate space for a single object of type T
  /// @return Pointer to allocated memory (uninitialized)
  template <typename T>
  T* Allocate() {
    return static_cast<T*>(Allocate(sizeof(T)));
  }

  /// @brief Allocate space for an array of count objects of type T
  /// @param count Number of elements
  /// @return Pointer to allocated memory (uninitialized)
  template <typename T>
  T* AllocateArray(size_t count) {
    return static_cast<T*>(Allocate(count * sizeof(T)));
  }

  /// @brief Align the current position to the given alignment
  /// @param alignment Alignment in bytes (must be power of 2)
  void Align(size_t alignment) {
    ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0);
    uintptr_t pos = reinterpret_cast<uintptr_t>(base_) + used_;
    uintptr_t aligned_pos = (pos + alignment - 1) & ~(alignment - 1);
    used_ = aligned_pos - reinterpret_cast<uintptr_t>(base_);
  }

  /// @brief Get the current position in the arena
  uint8_t* CurrentPosition() { return base_ + used_; }

  /// @brief Get the number of bytes remaining
  size_t BytesRemaining() const { return capacity_ - used_; }

  /// @brief Get the number of bytes used
  size_t BytesUsed() const { return used_; }

  /// @brief Reset the arena (frees all allocations)
  void Reset() {
    used_ = 0;
    temp_count_ = 0;
  }

 private:
  void Free() noexcept {
    if (base_ == nullptr) {
      return;
    }
    ::operator delete(base_, std::align_val_t(kAlignment));
    base_ = nullptr;
    capacity_ = 0;
    used_ = 0;
    temp_count_ = 0;
  }

  friend class ScopedArenaMemory;

  uint8_t* base_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
  int32_t temp_count_ = 0;
};

/// @brief RAII scoped arena memory region
///
/// Creates a temporary scope within an arena. When this object is destroyed,
/// the arena is rewound to its state when the scope was created. This allows
/// for stack-like allocation patterns.
///
/// Example:
/// @code
///   Arena arena(1024);
///   {
///     ScopedArenaMemory scope(&arena);
///     auto *data = scope.Allocate<int>(100);
///     // ... use data ...
///   } // data is automatically freed here
/// @endcode
class ScopedArenaMemory {
 public:
  /// @brief Begin a scoped memory region on the given arena
  explicit ScopedArenaMemory(Arena* arena)
      : arena_(arena),
        saved_used_(arena->used_),
        temp_index_(arena->temp_count_) {
    ++arena_->temp_count_;
  }

  /// @brief Release the scoped memory (rewind arena to saved position)
  ~ScopedArenaMemory() {
    if (arena_) {
      ASSERT(arena_->temp_count_ > 0);
      --arena_->temp_count_;
      arena_->used_ = saved_used_;
      ASSERT(temp_index_ == arena_->temp_count_);
    }
  }

  // Delete copy operations
  ScopedArenaMemory(const ScopedArenaMemory&) = delete;
  ScopedArenaMemory& operator=(const ScopedArenaMemory&) = delete;

  // Move operations
  ScopedArenaMemory(ScopedArenaMemory&& other) noexcept
      : arena_(other.arena_),
        saved_used_(other.saved_used_),
        temp_index_(other.temp_index_) {
    other.arena_ = nullptr;
  }

  ScopedArenaMemory& operator=(ScopedArenaMemory&& other) noexcept {
    if (this != &other) {
      if (arena_) {
        ASSERT(arena_->temp_count_ > 0);
        --arena_->temp_count_;
        arena_->used_ = saved_used_;
      }
      arena_ = other.arena_;
      saved_used_ = other.saved_used_;
      temp_index_ = other.temp_index_;
      other.arena_ = nullptr;
    }
    return *this;
  }

  /// @brief Allocate size bytes from the arena
  void* Allocate(size_t size) { return arena_->Allocate(size); }

  /// @brief Allocate space for a single object of type T
  template <typename T>
  T* Allocate() {
    return arena_->Allocate<T>();
  }

  /// @brief Allocate space for an array of count objects of type T
  template <typename T>
  T* AllocateArray(size_t count) {
    return arena_->AllocateArray<T>(count);
  }

  /// @brief Align the arena to the given alignment
  void Align(size_t alignment) { arena_->Align(alignment); }

  /// @brief Get the underlying arena
  Arena* GetArena() { return arena_; }

 private:
  Arena* arena_ = nullptr;
  size_t saved_used_ = 0;
  int32_t temp_index_ = 0;
};

/// @brief Thread-local arena for temporary allocations
///
/// Provides access to a thread-local arena for fast temporary allocations.
/// Each thread gets its own Arena instance (via thread_local storage), ensuring
/// thread safety without locks.
///
/// Thread safety: SAFE - Each thread has its own Arena instance.
/// The Arena itself is not thread-safe, but since each thread has its own,
/// no synchronization is needed.
///
/// Example:
/// @code
///   // Each thread automatically gets its own 256MB arena
///   auto scope = ThreadLocalArena::BeginScope();
///   auto *data = scope.AllocateArray<float>(1000);
///   // ... use data ...
///   // Automatically freed when scope ends
/// @endcode
class ThreadLocalArena {
 public:
  /// @brief Get the thread-local arena, creating it on first access per thread
  /// @return Reference to this thread's Arena (each thread has its own)
  static aifocore::Result<Arena*> Get();

  /// @brief Create a scoped memory region on the thread-local arena
  /// @return RAII scope that automatically rewinds on destruction
  static aifocore::Result<ScopedArenaMemory> BeginScope() {
    AIFOCORE_ASSIGN_OR_RETURN(Arena * arena, Get());
    return ScopedArenaMemory(arena);
  }

 private:
  ThreadLocalArena() = delete;
};

}  // namespace isyntax
