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

#include "fastslide/utilities/tiff/tiff_pool.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "aifocore/status/status_macros.h"

namespace fs = std::filesystem;

namespace fastslide {

// Initialize static pool id counter
std::atomic<uint64_t> TIFFHandlePool::next_pool_id_{0};

// Thread-local storage (TLS) handle cache for fast-path access, per pool
// Each thread maintains its own one-slot cache to avoid synchronization
// overhead. The cached handle is associated with the specific pool instance.
thread_local TIFFHandlePool::TIFFCacheSlot TIFFHandlePool::tls_slot_{nullptr,
                                                                     0};

TIFFHandleGuard::~TIFFHandleGuard() noexcept {
  Release();
}

void TIFFHandleGuard::Release() {
  if (handle_ != nullptr && pool_ != nullptr) {
    pool_->Release(handle_);
    handle_ = nullptr;
    pool_ = nullptr;
  }
}

absl::StatusOr<std::unique_ptr<TIFFHandlePool>> TIFFHandlePool::Create(
    const fs::path& path, unsigned pool_size) {
  // Try to open the initial TIFF file first
  TIFF* initial_tif = TIFFOpen(path.string().c_str(), "rm");
  if (initial_tif == nullptr) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Cannot open TIFF file: " + path.string());
  }

  // Create the pool using private constructor
  auto pool =
      std::unique_ptr<TIFFHandlePool>(new TIFFHandlePool(path, pool_size));

  // Initialize the node pool with pre-allocated nodes
  pool->InitializeNodePool();

  // Set up the initial handle and semaphore
  pool->PushLockFree(initial_tif);
  pool->total_handles_opened_.store(1);
  pool->free_count_.store(1);

  // Initialize semaphore with max_pool_size permits
  // (representing total capacity)
  for (unsigned i = 0; i < pool->max_pool_size_; ++i) {
    pool->semaphore_.release();
  }
  // No need to do std::move(pool) here, modern compilers are smart
  return pool;
}

TIFFHandlePool::TIFFHandlePool(fs::path path, unsigned pool_size)
    : path_(std::move(path)),
      c_path_(path_.string()),
      c_path_ptr_(c_path_.c_str()),
      pool_id_(++next_pool_id_),
      max_pool_size_(pool_size),
      total_handles_opened_(0),
      free_count_(0) {

  // Auto-detect pool size based on hardware concurrency
  if (max_pool_size_ == 0) {
    max_pool_size_ = std::max(1U, std::thread::hardware_concurrency());
  }
  // Note: Initialization of the first handle is done in Create()
}

TIFFHandlePool::~TIFFHandlePool() {
  // Set shutdown flag
  shutdown_flag_.store(true);

  // If the current thread's TLS cache holds a handle for this pool,
  // close it to prevent stale cross-pool reuse when a new pool is created
  // at the same address.
  if (tls_slot_.pool_id == pool_id_ && tls_slot_.handle != nullptr) {
    TIFFClose(tls_slot_.handle);
    tls_slot_.handle = nullptr;
    tls_slot_.pool_id = 0;
  }

  // Wait a bit for threads to wake up and exit
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Clean up all handles in the lock-free stack
  CleanupLockFreeStack();

  // Clean up the node pool
  CleanupNodePool();

  // Note: TLS handles will be cleaned up automatically when threads exit
  // We don't need to explicitly close them here as they're thread-local
  // and will be destroyed when the thread terminates
}

TIFFHandleGuard TIFFHandlePool::Acquire(std::chrono::milliseconds timeout) {
  // Check shutdown flag first
  if (shutdown_flag_.load()) {
    return {nullptr, this};
  }

  // FAST PATH: Check thread-local storage (TLS) cache first (bound to this pool)
  if (tls_slot_.handle != nullptr && tls_slot_.pool_id == pool_id_) {
    TIFF* cached_handle = tls_slot_.handle;
    tls_slot_.handle = nullptr;
    tls_slot_.pool_id = 0;
    return {cached_handle, this};
  }

  // SLOW PATH: Fall back to semaphore + lock-free stack synchronization
  // Try to acquire a permit from the semaphore
  bool permit_acquired = false;
  if (timeout.count() == 0) {
    // No timeout - wait indefinitely, track waiting threads while blocked
    waiting_threads_.fetch_add(1, std::memory_order_relaxed);
    semaphore_.acquire();
    waiting_threads_.fetch_sub(1, std::memory_order_relaxed);
    permit_acquired = true;
  } else {
    // Wait with timeout, track waiting threads during wait window
    waiting_threads_.fetch_add(1, std::memory_order_relaxed);
    permit_acquired = semaphore_.try_acquire_for(timeout);
    waiting_threads_.fetch_sub(1, std::memory_order_relaxed);
  }

  if (!permit_acquired) {
    return {nullptr, this};
  }

  // Check shutdown flag after acquiring permit
  if (shutdown_flag_.load()) {
    // Release the permit back since we're shutting down
    semaphore_.release();
    return {nullptr, this};
  }

  // Now we have a permit, try to get a handle from the lock-free stack
  TIFF* handle = PopLockFree();

  if (handle != nullptr) {
    free_count_.fetch_sub(1);
    return {handle, this};
  }

  // No existing handles, but we have a permit - try to open a new one
  if (total_handles_opened_.load() < max_pool_size_) {
    total_handles_opened_.fetch_add(1);  // Reserve capacity

    TIFF* new_handle = TryOpenNewHandle();

    if (new_handle != nullptr) {
      return {new_handle, this};  // hand it directly to caller
    }
    // open failed—roll back reservation
    total_handles_opened_.fetch_sub(1);
  } else {
    LOG(WARNING)
        << "Cannot open new handle, at max capacity. total_handles_opened: "
        << total_handles_opened_.load() << ", max: " << max_pool_size_;
  }

  // Failed to get or create a handle, but we acquired a permit
  // Release the permit back
  LOG(INFO) << "Failed to get or create handle, releasing permit back";
  semaphore_.release();
  return {nullptr, this};
}

TIFFHandleGuard TIFFHandlePool::TryAcquire() {
  // Check shutdown flag first
  if (shutdown_flag_.load()) {
    LOG(INFO) << "Pool is shutting down, returning nullptr";
    return {nullptr, this};
  }

  // FAST PATH: Check thread-local storage (TLS) cache first (bound to this pool)
  if (tls_slot_.handle != nullptr && tls_slot_.pool_id == pool_id_) {
    TIFF* cached_handle = tls_slot_.handle;
    tls_slot_.handle = nullptr;
    tls_slot_.pool_id = 0;
    return {cached_handle, this};
  }

  // SLOW PATH: Fall back to semaphore + lock-free stack synchronization
  // Try to acquire a permit without blocking
  if (!semaphore_.try_acquire()) {
    LOG(INFO) << "No semaphore permit available, returning nullptr";
    return {nullptr, this};
  }

  // Check shutdown flag after acquiring permit
  if (shutdown_flag_.load()) {
    LOG(INFO) << "Pool shutting down after permit acquisition in TryAcquire, "
                 "releasing permit";
    semaphore_.release();
    return {nullptr, this};
  }

  // Now we have a permit, try to get a handle from the lock-free stack
  TIFF* handle = PopLockFree();

  if (handle != nullptr) {
    free_count_.fetch_sub(1);
    return {handle, this};
  }

  // No existing handles, but we have a permit - try to open a new one
  if (total_handles_opened_.load() < max_pool_size_) {
    total_handles_opened_.fetch_add(1);  // Reserve capacity

    TIFF* new_handle = TryOpenNewHandle();

    if (new_handle != nullptr) {
      return {new_handle, this};  // Hand it directly to caller
    }
    // open failed—roll back reservation
    LOG(INFO)
        << "TryAcquire: failed to open new handle, rolling back reservation";
    total_handles_opened_.fetch_sub(1);
  } else {
    LOG(INFO) << "TryAcquire: cannot open new handle, at max capacity";
  }

  // Failed to get or create a handle, but we acquired a permit
  // Release the permit back
  LOG(INFO)
      << "TryAcquire: failed to get or create handle, releasing permit back";
  semaphore_.release();
  return {nullptr, this};
}

void TIFFHandlePool::Release(TIFF* handle) {
  if (handle == nullptr) {
    return;
  }

  // If pool is shutting down, close the handle directly
  if (shutdown_flag_.load()) {
    TIFFClose(handle);
    return;
  }

  // Prefer waking waiters: if there are waiting threads, return handle to global pool
  if (waiting_threads_.load(std::memory_order_relaxed) == 0) {
    // No waiters: try to stash handle in thread-local storage (TLS) cache first,
    // bound to this pool instance. Only cache if slot is empty.
    if (tls_slot_.handle == nullptr) {
      tls_slot_.handle = handle;
      tls_slot_.pool_id = pool_id_;
      // Do NOT release a permit here since the handle is not globally available.
      // The permit remains consumed and the same thread can reacquire without synchronization.
      return;
    }
  }

  // SLOW PATH: TLS cache is full, fall back to lock-free stack
  // This requires semaphore synchronization but no mutex
  PushLockFree(handle);
  free_count_.fetch_add(1, std::memory_order_relaxed);
  // Release the permit that was consumed when handle was acquired
  semaphore_.release();
}

TIFF* TIFFHandlePool::TryOpenNewHandle() {
  TIFF* new_handle = TIFFOpen(c_path_ptr_, "rm");
  return new_handle;  // Returns nullptr if TIFFOpen fails
}

void TIFFHandlePool::PushLockFree(TIFF* handle) {
  // Get a node from the pool instead of allocating new
  Node* new_node = GetNodeFromPool();
  new_node->tif = handle;
  new_node->next = nullptr;

  // Lock-free push using compare-and-swap
  Node* expected_head = head_.load(std::memory_order_relaxed);
  while (!head_.compare_exchange_weak(new_node->next, new_node,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
    new_node->next = expected_head;
    expected_head = head_.load(std::memory_order_relaxed);
  }
}

TIFF* TIFFHandlePool::PopLockFree() {
  Node* current_head = head_.load(std::memory_order_acquire);
  Node* new_head = (current_head != nullptr) ? current_head->next : nullptr;

  // Lock-free pop using compare-and-swap
  while (true) {
    if (current_head == nullptr) {
      return nullptr;  // Stack is empty
    }
    new_head = current_head->next;
    if (head_.compare_exchange_weak(current_head, new_head,
                                    std::memory_order_acquire,
                                    std::memory_order_relaxed)) {
      break;
    }
  }

  // Extract the handle and return the node to the pool
  TIFF* handle = current_head->tif;
  ReturnNodeToPool(current_head);
  return handle;
}

void TIFFHandlePool::CleanupLockFreeStack() {
  // Pop all remaining handles and close them
  TIFF* handle;
  while ((handle = PopLockFree()) != nullptr) {
    TIFFClose(handle);
  }
}

void TIFFHandlePool::InitializeNodePool() {
  // Pre-allocate max_pool_size_ nodes and push them onto the node pool
  for (unsigned i = 0; i < max_pool_size_; ++i) {
    auto* new_node = new TIFFHandlePool::Node{.tif = nullptr, .next = nullptr};
    ReturnNodeToPool(new_node);
  }
}

TIFFHandlePool::Node* TIFFHandlePool::GetNodeFromPool() {
  TIFFHandlePool::Node* current_head;
  TIFFHandlePool::Node* new_head;

  // Try to pop a node from the pool first
  while (true) {
    current_head = node_pool_head_.load(std::memory_order_acquire);
    if (current_head == nullptr) {
      // Pool is empty, allocate a new node as fallback
      return new TIFFHandlePool::Node{.tif = nullptr, .next = nullptr};
    }
    new_head = current_head->next;
    if (node_pool_head_.compare_exchange_weak(current_head, new_head,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
      break;
    }
  }

  // Reset the node for reuse
  current_head->tif = nullptr;
  current_head->next = nullptr;
  return current_head;
}

void TIFFHandlePool::ReturnNodeToPool(TIFFHandlePool::Node* node) {
  if (node == nullptr) {
    return;
  }

  // Lock-free push to node pool using compare-and-swap
  TIFFHandlePool::Node* expected_head =
      node_pool_head_.load(std::memory_order_relaxed);
  while (!node_pool_head_.compare_exchange_weak(
      node->next, node, std::memory_order_release, std::memory_order_relaxed)) {
    node->next = expected_head;
    expected_head = node_pool_head_.load(std::memory_order_relaxed);
  }
}

void TIFFHandlePool::CleanupNodePool() {
  // Pop all remaining nodes from the pool and deallocate them
  TIFFHandlePool::Node* current_head =
      node_pool_head_.load(std::memory_order_acquire);
  TIFFHandlePool::Node* new_head;

  while (current_head != nullptr) {
    new_head = current_head->next;
    if (node_pool_head_.compare_exchange_weak(current_head, new_head,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
      break;
    }
    current_head = node_pool_head_.load(std::memory_order_acquire);
  }

  // Deallocate all nodes in the chain
  while (current_head != nullptr) {
    TIFFHandlePool::Node* next = current_head->next;
    // This is a custom memory pool where we explicitly manage the lifetime of
    // the nodes. So, we can safely ignore the linter warning.
    delete current_head;  // NOLINT(cppcoreguidelines-owning-memory)
    current_head = next;
  }
}

}  // namespace fastslide
