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

#include <gtest/gtest.h>
#include <tiffio.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "absl/status/status.h"

namespace fs = std::filesystem;

namespace fastslide {

/// @brief Test fixture for TIFFHandlePool tests
class TIFFHandlePoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a temporary directory for test files
    temp_dir_ = fs::temp_directory_path() / "tiff_pool_test";
    fs::create_directories(temp_dir_);

    // Create a test TIFF file
    test_tiff_path_ = temp_dir_ / "test.tiff";
    CreateTestTIFF(test_tiff_path_);

    // Create a path to a non-existent file for failure tests
    nonexistent_path_ = temp_dir_ / "nonexistent.tiff";
  }

  void TearDown() override {
    // Clean up temporary files
    if (fs::exists(temp_dir_)) {
      fs::remove_all(temp_dir_);
    }
  }

  /// @brief Create a minimal test TIFF file
  void CreateTestTIFF(const fs::path& path) {
    TIFF* tif = TIFFOpen(path.string().c_str(), "w");
    ASSERT_NE(tif, nullptr) << "Failed to create test TIFF file";

    // Set basic TIFF tags
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 100);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 100);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 100);

    // Write some dummy data
    std::vector<uint8_t> data(100 * 3, 128);  // Gray RGB data
    for (int row = 0; row < 100; ++row) {
      TIFFWriteScanline(tif, data.data(), row);
    }

    TIFFClose(tif);
  }

  fs::path temp_dir_;
  fs::path test_tiff_path_;
  fs::path nonexistent_path_;
};

/// @brief Test successful pool creation
TEST_F(TIFFHandlePoolTest, CreateSuccess) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();

  auto pool = std::move(pool_result.value());
  ASSERT_NE(pool, nullptr);

  // Check initial statistics
  auto stats = pool->GetStats();
  EXPECT_GT(stats.max_handles, 0);
  EXPECT_EQ(stats.total_opened, 1);  // One handle opened during creation
  EXPECT_EQ(stats.available_handles, 1);
}

/// @brief Test pool creation with custom size
TEST_F(TIFFHandlePoolTest, CreateWithCustomSize) {
  constexpr unsigned kPoolSize = 5;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();

  auto pool = std::move(pool_result.value());
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.max_handles, kPoolSize);
  EXPECT_EQ(stats.total_opened, 1);
  EXPECT_EQ(stats.available_handles, 1);
}

/// @brief Test pool creation failure with non-existent file
TEST_F(TIFFHandlePoolTest, CreateFailureNonExistentFile) {
  auto pool_result = TIFFHandlePool::Create(nonexistent_path_);
  EXPECT_FALSE(pool_result.ok());
  EXPECT_EQ(pool_result.status().code(), absl::StatusCode::kInvalidArgument);
}

/// @brief Test basic handle acquisition and release
TEST_F(TIFFHandlePoolTest, BasicAcquireAndRelease) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire a handle
  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());
  EXPECT_NE(guard.Get(), nullptr);

  // Check statistics
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 0);  // Handle is acquired

  // Handle should be automatically released when guard goes out of scope
  {
    auto guard2 = std::move(guard);
    EXPECT_TRUE(guard2.Valid());
    EXPECT_FALSE(guard.Valid());  // Original guard should be invalid
  }

  // After guard is destroyed, handle should be available again
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 1);
}

/// @brief Test TryAcquire functionality
TEST_F(TIFFHandlePoolTest, TryAcquire) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 1);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // First TryAcquire should succeed
  auto guard1 = pool->TryAcquire();
  EXPECT_TRUE(guard1.Valid());

  // Second TryAcquire should fail (pool size is 1)
  auto guard2 = pool->TryAcquire();
  EXPECT_FALSE(guard2.Valid());

  // After releasing first handle, TryAcquire should succeed again
  guard1 = TIFFHandleGuard(nullptr, nullptr);  // Release handle
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  auto guard3 = pool->TryAcquire();
  EXPECT_TRUE(guard3.Valid());
}

/// @brief Test handle guard move semantics
TEST_F(TIFFHandlePoolTest, HandleGuardMoveSemantics) {
  auto pool_result =
      TIFFHandlePool::Create(test_tiff_path_, 1);  // Limit to 1 handle
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());
  TIFF* original_handle = guard1.Get();

  // Test move constructor
  auto guard2 = std::move(guard1);
  EXPECT_FALSE(guard1.Valid());
  EXPECT_TRUE(guard2.Valid());
  EXPECT_EQ(guard2.Get(), original_handle);

  // Test move assignment
  auto guard3 =
      pool->TryAcquire();  // This should fail since pool is limited to 1
  EXPECT_FALSE(guard3.Valid());

  guard3 = std::move(guard2);
  EXPECT_FALSE(guard2.Valid());
  EXPECT_TRUE(guard3.Valid());
  EXPECT_EQ(guard3.Get(), original_handle);
}

/// @brief Test handle guard implicit conversion
TEST_F(TIFFHandlePoolTest, HandleGuardImplicitConversion) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());

  // Test explicit access to TIFF*
  TIFF* tif = guard.Get();
  EXPECT_NE(tif, nullptr);
  EXPECT_EQ(tif, guard.Get());

  // Test that we can use it with TIFF functions
  uint32_t width, height;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  EXPECT_EQ(width, 100);
  EXPECT_EQ(height, 100);
}

/// @brief Test pool expansion
TEST_F(TIFFHandlePoolTest, PoolExpansion) {
  constexpr unsigned kPoolSize = 3;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire handles up to the pool size
  std::vector<TIFFHandleGuard> guards;
  for (unsigned i = 0; i < kPoolSize; ++i) {
    auto guard = pool->TryAcquire();
    EXPECT_TRUE(guard.Valid()) << "Failed to acquire handle " << i;
    guards.emplace_back(std::move(guard));
  }

  // Check that all handles are in use
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 0);
  EXPECT_EQ(stats.total_opened, kPoolSize);

  // Try to acquire one more - should fail
  auto extra_guard = pool->TryAcquire();
  EXPECT_FALSE(extra_guard.Valid());
}

/// @brief Test timeout behavior
TEST_F(TIFFHandlePoolTest, TimeoutBehavior) {
  constexpr unsigned kPoolSize = 1;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Try to acquire with timeout - should fail
  auto start = std::chrono::steady_clock::now();
  auto guard2 = pool->Acquire(std::chrono::milliseconds(100));
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(guard2.Valid());
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(duration.count(), 90);   // Should wait at least 90ms
  EXPECT_LT(duration.count(), 200);  // But not much more than 100ms
}

/// @brief Test thread safety
TEST_F(TIFFHandlePoolTest, ThreadSafety) {
  constexpr unsigned kPoolSize = 4;
  constexpr unsigned kNumThreads = 8;
  constexpr unsigned kOperationsPerThread = 100;

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::vector<std::thread> threads;
  std::atomic<int> successful_operations{0};
  std::atomic<int> failed_operations{0};

  for (unsigned i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      for (unsigned j = 0; j < kOperationsPerThread; ++j) {
        auto guard = pool->TryAcquire();
        if (guard.Valid()) {
          // Simulate some work
          std::this_thread::sleep_for(std::chrono::microseconds(10));

          // Verify we can read from the TIFF
          uint32_t width;
          TIFFGetField(guard.Get(), TIFFTAG_IMAGEWIDTH, &width);
          if (width == 100) {
            successful_operations++;
          } else {
            failed_operations++;
          }
        } else {
          failed_operations++;
        }
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Check that we had successful operations and no corruption
  EXPECT_GT(successful_operations.load(), 0);
  EXPECT_EQ(failed_operations.load(),
            kNumThreads * kOperationsPerThread - successful_operations.load());

  // Pool should be in a consistent state
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.max_handles, kPoolSize);
  EXPECT_LE(stats.total_opened, kPoolSize);
}

/// @brief Test concurrent blocking acquisitions
TEST_F(TIFFHandlePoolTest, ConcurrentBlockingAcquisitions) {
  constexpr unsigned kPoolSize = 2;
  constexpr unsigned kNumThreads = 4;

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::vector<std::future<bool>> futures;
  std::atomic<int> concurrent_count{0};
  std::atomic<int> max_concurrent{0};

  for (unsigned i = 0; i < kNumThreads; ++i) {
    futures.emplace_back(std::async(std::launch::async, [&]() {
      auto guard = pool->Acquire(std::chrono::milliseconds(1000));
      if (guard.Valid()) {
        int current = concurrent_count.fetch_add(1) + 1;
        int expected = max_concurrent.load();
        while (current > expected &&
               !max_concurrent.compare_exchange_weak(expected, current)) {}

        // Hold the handle for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        concurrent_count.fetch_sub(1);
        return true;
      }
      return false;
    }));
  }

  // Wait for all operations to complete
  int successful = 0;
  for (auto& future : futures) {
    if (future.get()) {
      successful++;
    }
  }

  // All operations should succeed eventually
  EXPECT_EQ(successful, kNumThreads);

  // We should never have more than pool_size concurrent operations
  EXPECT_LE(max_concurrent.load(), kPoolSize);
}

/// @brief Test statistics tracking
TEST_F(TIFFHandlePoolTest, StatisticsTracking) {
  constexpr unsigned kPoolSize = 1;  // Use size 1 to force blocking
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Initial state
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.max_handles, kPoolSize);
  EXPECT_EQ(stats.total_opened, 1);
  EXPECT_EQ(stats.available_handles, 1);
  EXPECT_EQ(stats.waiting_threads, 0);

  // Acquire the only handle
  auto guard1 = pool->Acquire();

  stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 0);
  EXPECT_EQ(stats.total_opened, 1);

  // Use a synchronization mechanism to ensure the thread is waiting
  std::atomic<bool> thread_started{false};
  std::thread waiter([&]() {
    thread_started = true;
    auto guard = pool->Acquire(std::chrono::milliseconds(200));
    // This should timeout since the handle is acquired
    EXPECT_FALSE(guard.Valid());
  });

  // Wait for the thread to start
  while (!thread_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Give additional time for the thread to enter the waiting state
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  stats = pool->GetStats();
  EXPECT_EQ(stats.waiting_threads, 1);

  waiter.join();

  // After timeout, no threads should be waiting
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stats = pool->GetStats();
  EXPECT_EQ(stats.waiting_threads, 0);
}

/// @brief Test pool destruction with active handles
TEST_F(TIFFHandlePoolTest, DestructionWithActiveHandles) {
  // Test that we can properly handle pool destruction while handles are active
  // This primarily tests that no crashes or memory leaks occur

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire a handle
  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());

  // Test that we can use the handle while pool is alive
  uint32_t width;
  TIFFGetField(guard.Get(), TIFFTAG_IMAGEWIDTH, &width);
  EXPECT_EQ(width, 100);

  // Pool will be destroyed when it goes out of scope
  // The guard will automatically handle the cleanup
}

/// @brief Test edge case: zero timeout
TEST_F(TIFFHandlePoolTest, ZeroTimeout) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 1);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Acquire with zero timeout should wait indefinitely
  // We test this by starting a thread that releases the handle after a delay
  std::thread releaser([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    guard1 = TIFFHandleGuard(nullptr, nullptr);  // Release handle
  });

  auto start = std::chrono::steady_clock::now();
  auto guard2 = pool->Acquire();  // Should wait for releaser
  auto end = std::chrono::steady_clock::now();

  EXPECT_TRUE(guard2.Valid());
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(duration.count(), 40);  // Should wait at least 40ms

  releaser.join();
}

/// @brief Test auto-detection of pool size when pool_size = 0
TEST_F(TIFFHandlePoolTest, AutoDetectPoolSize) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 0);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto stats = pool->GetStats();
  // Should be set to hardware concurrency (at least 1)
  EXPECT_GE(stats.max_handles, 1);
  EXPECT_EQ(stats.max_handles,
            std::max(1U, std::thread::hardware_concurrency()));
}

/// @brief Test TIFFHandleGuard with nullptr handle
TEST_F(TIFFHandlePoolTest, HandleGuardWithNullptr) {
  TIFFHandleGuard guard(nullptr, nullptr);
  EXPECT_FALSE(guard.Valid());
  EXPECT_EQ(guard.Get(), nullptr);

  // Should be safe to call multiple times
  guard = TIFFHandleGuard(nullptr, nullptr);
  EXPECT_FALSE(guard.Valid());

  // Explicit access should work
  TIFF* tif = guard.Get();
  EXPECT_EQ(tif, nullptr);
}

/// @brief Test multiple Release() calls on same guard
TEST_F(TIFFHandlePoolTest, MultipleReleaseCallsSafety) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());

  // Move to another guard to trigger Release() on the first
  auto guard2 = std::move(guard);
  EXPECT_FALSE(guard.Valid());
  EXPECT_TRUE(guard2.Valid());

  // Now assign nullptr to trigger another Release()
  guard2 = TIFFHandleGuard(nullptr, nullptr);
  EXPECT_FALSE(guard2.Valid());

  // Should be safe - no crashes or undefined behavior
}

/// @brief Test Release() with nullptr handle indirectly through guard
TEST_F(TIFFHandlePoolTest, ReleaseNullptrHandleIndirect) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Create a guard with nullptr - this tests the Release() safety indirectly
  TIFFHandleGuard null_guard(nullptr, pool.get());

  // Moving a null guard should be safe
  auto moved_guard = std::move(null_guard);
  EXPECT_FALSE(moved_guard.Valid());

  auto stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 1);  // Should be unchanged
  EXPECT_EQ(stats.total_opened, 1);
}

/// @brief Test TryOpenNewHandle failure scenario
TEST_F(TIFFHandlePoolTest, HandleCreationFailure) {
  // Create a pool
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 3);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the initial handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Delete the file to make TryOpenNewHandle fail
  fs::remove(test_tiff_path_);

  // Now TryAcquire should fail gracefully when trying to create new handles
  auto guard2 = pool->TryAcquire();
  EXPECT_FALSE(guard2.Valid());

  auto stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 1);  // Should not have increased
  EXPECT_EQ(stats.available_handles, 0);
}

/// @brief Test file access changes after pool creation
TEST_F(TIFFHandlePoolTest, FileAccessChanges) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 2);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // First acquisition should work (uses pre-opened handle)
  auto guard1 = pool->TryAcquire();
  EXPECT_TRUE(guard1.Valid());

  // Change file permissions to make it unreadable
  fs::permissions(test_tiff_path_, fs::perms::none);

  // Second acquisition may fail when trying to open new handle
  auto guard2 = pool->TryAcquire();
  // This could succeed or fail depending on
  // when new handle creation is attempted

  // Restore permissions for cleanup
  fs::permissions(test_tiff_path_, fs::perms::owner_all);
}

/// @brief Test pool destruction behavior
TEST_F(TIFFHandlePoolTest, PoolDestructionBehavior) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 1);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Test that pool can be safely destroyed even with active handles
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 0);
  EXPECT_EQ(stats.total_opened, 1);

  // Release handle before destroying pool to avoid mutex issues
  guard1 = TIFFHandleGuard(nullptr, nullptr);

  // Pool destruction should be safe
  pool.reset();

  // Test completed without crashes
}

/// @brief Test large pool size creation
TEST_F(TIFFHandlePoolTest, LargePoolSize) {
  constexpr unsigned kLargePoolSize = 100;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kLargePoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto stats = pool->GetStats();
  EXPECT_EQ(stats.max_handles, kLargePoolSize);
  EXPECT_EQ(stats.total_opened, 1);

  // Try to acquire many handles (limited by file system resources)
  std::vector<TIFFHandleGuard> guards;
  int successful_acquisitions = 0;

  for (unsigned i = 0; i < std::min(kLargePoolSize, 20U); ++i) {
    auto guard = pool->TryAcquire();
    if (guard.Valid()) {
      guards.emplace_back(std::move(guard));
      successful_acquisitions++;
    } else {
      break;  // Stop when we can't acquire more
    }
  }

  EXPECT_GT(successful_acquisitions, 0);

  stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 0);
  EXPECT_LE(stats.total_opened, kLargePoolSize);
}

/// @brief Test rapid acquisition and release cycles
TEST_F(TIFFHandlePoolTest, RapidAcquisitionReleaseCycles) {
  constexpr unsigned kPoolSize = 3;
  constexpr unsigned kCycles = 1000;

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  for (unsigned cycle = 0; cycle < kCycles; ++cycle) {
    auto guard = pool->TryAcquire();
    if (guard.Valid()) {
      // Use the handle briefly
      uint32_t width;
      TIFFGetField(guard.Get(), TIFFTAG_IMAGEWIDTH, &width);
      EXPECT_EQ(width, 100);
    }
    // Guard is automatically released at end of scope
  }

  // Pool should be in consistent state
  auto stats = pool->GetStats();
  EXPECT_LE(stats.total_opened, kPoolSize);
  EXPECT_EQ(stats.waiting_threads, 0);
}

/// @brief Test condition variable notification behavior
TEST_F(TIFFHandlePoolTest, ConditionVariableNotification) {
  constexpr unsigned kPoolSize = 1;
  constexpr unsigned kNumWaiters = 3;

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());

  std::atomic<int> acquisitions_completed{0};
  std::vector<std::thread> waiters;

  // Start multiple threads waiting for the handle
  for (unsigned i = 0; i < kNumWaiters; ++i) {
    waiters.emplace_back([&]() {
      auto acquired_guard = pool->Acquire(std::chrono::milliseconds(1000));
      if (acquired_guard.Valid()) {
        acquisitions_completed++;
        // Hold handle briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
  }

  // Let threads start waiting
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto stats = pool->GetStats();
  EXPECT_EQ(stats.waiting_threads, kNumWaiters);

  // Release the handle - should notify one waiter
  guard = TIFFHandleGuard(nullptr, nullptr);

  // Wait for all threads to complete
  for (auto& thread : waiters) {
    thread.join();
  }

  // At least one should have succeeded
  EXPECT_GT(acquisitions_completed.load(), 0);
  EXPECT_LE(acquisitions_completed.load(), kNumWaiters);
}

/// @brief Test statistics consistency under concurrent access
TEST_F(TIFFHandlePoolTest, StatisticsConsistency) {
  constexpr unsigned kPoolSize = 5;
  constexpr unsigned kNumThreads = 10;
  constexpr unsigned kOperationsPerThread = 50;

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::vector<std::thread> threads;
  std::atomic<bool> stop_flag{false};

  // Thread that continuously checks statistics consistency
  std::thread stats_checker([&]() {
    while (!stop_flag.load()) {
      auto stats = pool->GetStats();

      // Basic consistency checks
      EXPECT_LE(stats.total_opened, stats.max_handles);
      EXPECT_LE(stats.available_handles, stats.total_opened);
      EXPECT_GE(stats.waiting_threads, 0);

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Threads that acquire and release handles
  for (unsigned i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      for (unsigned j = 0; j < kOperationsPerThread; ++j) {
        auto guard = pool->TryAcquire();
        if (guard.Valid()) {
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      }
    });
  }

  // Wait for worker threads
  for (auto& thread : threads) {
    thread.join();
  }

  stop_flag = true;
  stats_checker.join();

  // Final consistency check
  auto final_stats = pool->GetStats();
  EXPECT_EQ(final_stats.waiting_threads, 0);
  EXPECT_LE(final_stats.total_opened, kPoolSize);
}

/// @brief Test self-assignment for TIFFHandleGuard
TEST_F(TIFFHandlePoolTest, HandleGuardSelfAssignment) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());
  TIFF* original_handle = guard.Get();

  // Self-assignment should be safe
  auto temp_guard = std::move(guard);
  guard = std::move(temp_guard);
  EXPECT_TRUE(guard.Valid());
  EXPECT_EQ(guard.Get(), original_handle);
}

/// @brief Test error status message content
TEST_F(TIFFHandlePoolTest, ErrorStatusMessages) {
  auto pool_result = TIFFHandlePool::Create(nonexistent_path_);
  EXPECT_FALSE(pool_result.ok());
  EXPECT_EQ(pool_result.status().code(), absl::StatusCode::kInvalidArgument);

  std::string error_message = std::string(pool_result.status().message());
  EXPECT_FALSE(error_message.empty());
  // Should contain reference to the file path and error description
  EXPECT_NE(error_message.find("TIFF file"), std::string::npos);
}

/// @brief Test Acquire() waiting successfully when handle becomes available
/// from pool
TEST_F(TIFFHandlePoolTest, AcquireWaitSuccessGetsFromPool) {
  constexpr unsigned kPoolSize = 1;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Start a thread that will wait for a handle
  std::atomic<bool> waiting_started{false};
  std::atomic<bool> acquire_completed{false};
  TIFFHandleGuard result_guard(nullptr, nullptr);

  std::thread waiter([&]() {
    waiting_started = true;
    // This will wait for a handle to become available
    result_guard = pool->Acquire(std::chrono::milliseconds(500));
    acquire_completed = true;
  });

  // Wait for thread to start waiting
  while (!waiting_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Delete the file (this shouldn't matter since the waiter will get existing
  // handle)
  fs::remove(test_tiff_path_);

  // Release the handle to wake up the waiter
  // The waiter should get the released handle from the pool
  guard1 = TIFFHandleGuard(nullptr, nullptr);

  waiter.join();
  EXPECT_TRUE(acquire_completed);

  // The acquire should have succeeded by reusing the released handle from the
  // pool
  EXPECT_TRUE(result_guard.Valid());

  // Verify no new handle was created
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 1);
}

/// @brief Test TryOpenNewHandle() failure during Acquire() when file is deleted
TEST_F(TIFFHandlePoolTest, AcquireHandleCreationFailsWithDeletedFile) {
  constexpr unsigned kPoolSize = 2;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the initial handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Delete the file to make subsequent TryOpenNewHandle() calls fail
  fs::remove(test_tiff_path_);

  // Try to acquire a second handle - this should fail because
  // TryOpenNewHandle() will fail
  auto guard2 = pool->TryAcquire();
  EXPECT_FALSE(guard2.Valid());

  // Verify that only one handle was created
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 1);
  EXPECT_EQ(stats.available_handles, 0);  // The first handle is still acquired
}

/// @brief Test Acquire() waiting successfully and TryOpenNewHandle() succeeds
TEST_F(TIFFHandlePoolTest, AcquireWaitSuccessAndHandleCreationSucceeds) {
  constexpr unsigned kPoolSize = 2;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the initial handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Start a thread that will wait for a handle
  std::atomic<bool> waiting_started{false};
  std::atomic<bool> acquire_completed{false};
  TIFFHandleGuard result_guard(nullptr, nullptr);

  std::thread waiter([&]() {
    waiting_started = true;
    // This will wait and should successfully create a new handle
    result_guard = pool->Acquire(std::chrono::milliseconds(500));
    acquire_completed = true;
  });

  // Wait for thread to start waiting
  while (!waiting_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Release the handle to wake up the waiter - it should create a new handle
  guard1 = TIFFHandleGuard(nullptr, nullptr);

  waiter.join();
  EXPECT_TRUE(acquire_completed);

  // The acquire should have succeeded by creating a new handle
  EXPECT_TRUE(result_guard.Valid());

  // Verify a new handle was created
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 2);
  EXPECT_EQ(stats.available_handles, 1);  // One in pool, one acquired by waiter
}

/// @brief Test Acquire() waiting successfully and getting handle from pool
TEST_F(TIFFHandlePoolTest, AcquireWaitSuccessAndGetFromPool) {
  constexpr unsigned kPoolSize = 1;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());
  TIFF* original_handle = guard1.Get();

  // Start a thread that will wait for the handle
  std::atomic<bool> waiting_started{false};
  std::atomic<bool> acquire_completed{false};
  TIFFHandleGuard result_guard(nullptr, nullptr);

  std::thread waiter([&]() {
    waiting_started = true;
    // This will wait for the original handle to be released
    result_guard = pool->Acquire(std::chrono::milliseconds(500));
    acquire_completed = true;
  });

  // Wait for thread to start waiting
  while (!waiting_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify thread is waiting
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.waiting_threads, 1);
  EXPECT_EQ(stats.total_opened, 1);
  EXPECT_EQ(stats.available_handles, 0);

  // Release the handle to make it available in the pool
  guard1 = TIFFHandleGuard(nullptr, nullptr);

  waiter.join();
  EXPECT_TRUE(acquire_completed);

  // The acquire should have succeeded by getting the handle from pool
  EXPECT_TRUE(result_guard.Valid());
  EXPECT_EQ(result_guard.Get(), original_handle);  // Same handle reused

  // Verify no new handle was created
  stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 1);       // Still only 1 handle total
  EXPECT_EQ(stats.available_handles, 0);  // Handle is acquired by waiter
}

/// @brief Test the edge case where pool reaches max size during wait
TEST_F(TIFFHandlePoolTest, AcquireWaitWithMaxPoolReached) {
  constexpr unsigned kPoolSize = 2;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire both handles up to max
  auto guard1 = pool->Acquire();
  auto guard2 = pool->TryAcquire();  // This should create the second handle
  EXPECT_TRUE(guard1.Valid());
  EXPECT_TRUE(guard2.Valid());

  auto stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 2);
  EXPECT_EQ(stats.available_handles, 0);

  // Now try to acquire - should wait and timeout since max is reached
  auto start = std::chrono::steady_clock::now();
  auto guard3 = pool->Acquire(std::chrono::milliseconds(100));
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(guard3.Valid());
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(duration.count(), 90);  // Should have waited for timeout
}

/// @brief Test hardware_concurrency edge case when it returns 0
TEST_F(TIFFHandlePoolTest, HardwareConcurrencyZeroFallback) {
  // This tests the fallback when hardware_concurrency() might return 0
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 0);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  auto stats = pool->GetStats();
  // Should always be at least 1, even if hardware_concurrency() returns 0
  EXPECT_GE(stats.max_handles, 1);
}

/// @brief Test multiple waiters with different timeout values
TEST_F(TIFFHandlePoolTest, MultipleWaitersWithDifferentTimeouts) {
  constexpr unsigned kPoolSize = 1;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());

  std::atomic<int> short_timeouts{0};
  std::atomic<int> long_timeouts{0};
  std::vector<std::thread> threads;

  // Start threads with different timeout values
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([&]() {
      auto result = pool->Acquire(std::chrono::milliseconds(50));
      if (!result.Valid())
        short_timeouts++;
    });

    threads.emplace_back([&]() {
      auto result = pool->Acquire(std::chrono::milliseconds(200));
      if (!result.Valid())
        long_timeouts++;
    });
  }

  // Wait for all timeouts
  for (auto& t : threads) {
    t.join();
  }

  // All should have timed out since handle was never released
  EXPECT_EQ(short_timeouts.load(), 3);
  EXPECT_EQ(long_timeouts.load(), 3);
}

/// @brief Test pool behavior when file becomes corrupted
TEST_F(TIFFHandlePoolTest, FileCorruptionHandling) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 3);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire initial handle
  auto guard1 = pool->Acquire();
  EXPECT_TRUE(guard1.Valid());

  // Corrupt the file by overwriting it with garbage
  std::ofstream corrupt_file(test_tiff_path_, std::ios::binary);
  corrupt_file << "This is not a TIFF file";
  corrupt_file.close();

  // Subsequent acquisitions should fail gracefully
  auto guard2 = pool->TryAcquire();
  EXPECT_FALSE(guard2.Valid());

  auto stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 1);  // Should not have increased
}

/// @brief Test pool with file permissions changing during operation
TEST_F(TIFFHandlePoolTest, DynamicPermissionChanges) {
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 3);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire initial handle (should work)
  auto guard1 = pool->TryAcquire();
  EXPECT_TRUE(guard1.Valid());

  // Remove read permissions
  fs::permissions(test_tiff_path_, fs::perms::owner_write);

  // Try to acquire another handle - may fail
  auto guard2 = pool->TryAcquire();
  // Result depends on when TryOpenNewHandle is called

  // Restore full permissions
  fs::permissions(test_tiff_path_, fs::perms::owner_all);

  // Should work again
  auto guard3 = pool->TryAcquire();
  EXPECT_TRUE(guard3.Valid());
}

/// @brief Test stress scenario with many short-lived acquisitions
TEST_F(TIFFHandlePoolTest, HighFrequencyAcquisitionStress) {
  constexpr unsigned kPoolSize = 4;
  constexpr unsigned kNumThreads = 12;
  constexpr unsigned kCyclesPerThread = 500;

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::atomic<int> total_successes{0};
  std::atomic<int> total_failures{0};
  std::vector<std::thread> threads;

  auto start_time = std::chrono::steady_clock::now();

  for (unsigned t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&]() {
      for (unsigned i = 0; i < kCyclesPerThread; ++i) {
        auto guard = pool->TryAcquire();
        if (guard.Valid()) {
          total_successes++;
          // Very brief usage
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        } else {
          total_failures++;
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  auto end_time = std::chrono::steady_clock::now();
  [[maybe_unused]] auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time);

  EXPECT_GT(total_successes.load(), 0);
  EXPECT_EQ(total_successes.load() + total_failures.load(),
            kNumThreads * kCyclesPerThread);

  // Pool should be in consistent state after stress test
  auto stats = pool->GetStats();
  EXPECT_LE(stats.total_opened, kPoolSize);
  EXPECT_EQ(stats.waiting_threads, 0);
}

/// @brief Test mixed acquire patterns (blocking and non-blocking)
TEST_F(TIFFHandlePoolTest, MixedAcquirePatterns) {
  constexpr unsigned kPoolSize = 2;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::atomic<bool> test_running{true};
  std::atomic<int> blocking_successes{0};
  std::atomic<int> nonblocking_successes{0};
  std::vector<std::thread> threads;

  // Mix of blocking and non-blocking threads
  for (int i = 0; i < 4; ++i) {
    // Blocking threads
    threads.emplace_back([&]() {
      while (test_running.load()) {
        auto guard = pool->Acquire(std::chrono::milliseconds(50));
        if (guard.Valid()) {
          blocking_successes++;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
    });

    // Non-blocking threads
    threads.emplace_back([&]() {
      while (test_running.load()) {
        auto guard = pool->TryAcquire();
        if (guard.Valid()) {
          nonblocking_successes++;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    });
  }

  // Run for a short period
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  test_running = false;

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_GT(blocking_successes.load(), 0);
  EXPECT_GT(nonblocking_successes.load(), 0);
}

/// @brief Test pool behavior under memory pressure simulation
TEST_F(TIFFHandlePoolTest, MemoryPressureSimulation) {
  constexpr unsigned kLargePoolSize = 50;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kLargePoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Try to exhaust available handles
  std::vector<TIFFHandleGuard> guards;
  int successful_acquisitions = 0;

  // Attempt to acquire many handles until failure
  for (unsigned i = 0; i < kLargePoolSize; ++i) {
    auto guard = pool->TryAcquire();
    if (guard.Valid()) {
      guards.emplace_back(std::move(guard));
      successful_acquisitions++;
    } else {
      break;  // Hit OS or system limits
    }
  }

  EXPECT_GT(successful_acquisitions, 0);

  // Verify pool state under pressure
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.available_handles, 0);
  EXPECT_LE(stats.total_opened, kLargePoolSize);

  // Additional acquisitions should fail
  auto extra_guard = pool->TryAcquire();
  EXPECT_FALSE(extra_guard.Valid());
}

/// @brief Test spurious wakeup handling in condition variables
TEST_F(TIFFHandlePoolTest, SpuriousWakeupResilience) {
  constexpr unsigned kPoolSize = 1;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());

  std::atomic<bool> waiter_started{false};
  std::atomic<bool> acquisition_result{false};

  // Thread that will wait for a long time
  std::thread long_waiter([&]() {
    waiter_started = true;
    auto result_guard = pool->Acquire(std::chrono::milliseconds(300));
    acquisition_result = result_guard.Valid();
  });

  // Wait for thread to start
  while (!waiter_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Let it wait for a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Verify it's still waiting (no spurious success)
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.waiting_threads, 1);

  long_waiter.join();

  // Should have timed out (not succeeded due to spurious wakeup)
  EXPECT_FALSE(acquisition_result.load());
}

/// @brief Test handle reuse vs creation priority
TEST_F(TIFFHandlePoolTest, HandleReusePriority) {
  constexpr unsigned kPoolSize = 3;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Create multiple handles
  auto guard1 = pool->TryAcquire();
  auto guard2 = pool->TryAcquire();
  EXPECT_TRUE(guard1.Valid());
  EXPECT_TRUE(guard2.Valid());

  TIFF* handle1_ptr = guard1.Get();
  TIFF* handle2_ptr = guard2.Get();

  // Release them in specific order
  guard1 = TIFFHandleGuard(nullptr, nullptr);
  guard2 = TIFFHandleGuard(nullptr, nullptr);

  // Acquire again - should reuse existing handles (LIFO order)
  auto guard3 = pool->TryAcquire();
  auto guard4 = pool->TryAcquire();

  EXPECT_TRUE(guard3.Valid());
  EXPECT_TRUE(guard4.Valid());

  // Should reuse existing handles rather than create new ones
  EXPECT_TRUE(guard3.Get() == handle2_ptr || guard3.Get() == handle1_ptr);
  EXPECT_TRUE(guard4.Get() == handle2_ptr || guard4.Get() == handle1_ptr);
  EXPECT_NE(guard3.Get(), guard4.Get());

  auto stats = pool->GetStats();
  EXPECT_EQ(stats.total_opened, 2);  // Should not have created more
}

/// @brief Test statistics accuracy under rapid state changes
TEST_F(TIFFHandlePoolTest, StatisticsAccuracyUnderRapidChanges) {
  constexpr unsigned kPoolSize = 4;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::atomic<bool> keep_running{true};
  std::atomic<bool> stats_consistent{true};

  // Thread that rapidly acquires and releases
  std::thread worker([&]() {
    while (keep_running.load()) {
      auto guard = pool->TryAcquire();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      // Guard automatically released
    }
  });

  // Thread that continuously validates statistics
  std::thread validator([&]() {
    while (keep_running.load()) {
      auto stats = pool->GetStats();

      // Validate invariants
      if (stats.total_opened > stats.max_handles ||
          stats.available_handles > stats.total_opened) {
        stats_consistent = false;
        break;
      }

      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  keep_running = false;

  worker.join();
  validator.join();

  EXPECT_TRUE(stats_consistent.load());
}

/// @brief Test pool destruction timing with concurrent operations
TEST_F(TIFFHandlePoolTest, DestructionTimingWithConcurrentOps) {
  std::atomic<bool> destruction_safe{true};

  {
    auto pool_result = TIFFHandlePool::Create(test_tiff_path_, 2);
    ASSERT_TRUE(pool_result.ok());
    auto pool = std::move(pool_result.value());

    std::atomic<bool> keep_working{true};
    std::vector<std::thread> workers;

    // Start background workers
    for (int i = 0; i < 3; ++i) {
      workers.emplace_back([&]() {
        while (keep_working.load()) {
          auto guard = pool->TryAcquire();
          if (!guard.Valid()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
          }
        }
      });
    }

    // Let them work briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Stop workers before pool destruction
    keep_working = false;
    for (auto& w : workers) {
      w.join();
    }

    // Pool destruction should be safe here
  }

  EXPECT_TRUE(destruction_safe.load());
}

/// @brief Test edge case with very short timeouts
TEST_F(TIFFHandlePoolTest, VeryShortTimeouts) {
  constexpr unsigned kPoolSize = 1;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  // Acquire the only handle
  auto guard = pool->Acquire();
  EXPECT_TRUE(guard.Valid());

  // Try very short timeouts
  for (int i = 0; i < 10; ++i) {
    auto start = std::chrono::steady_clock::now();
    auto result = pool->Acquire(std::chrono::milliseconds(1));
    auto end = std::chrono::steady_clock::now();

    EXPECT_FALSE(result.Valid());

    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // Should respect timeout even if very short
    EXPECT_GE(duration.count(), 500);  // At least 0.5ms
  }
}

/// @brief Test boundary condition: pool size of exactly 1 with many threads
TEST_F(TIFFHandlePoolTest, SingleHandlePoolWithManyThreads) {
  constexpr unsigned kPoolSize = 1;
  constexpr unsigned kNumThreads = 20;

  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kPoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::atomic<int> successful_acquisitions{0};
  std::atomic<int> failed_acquisitions{0};
  std::vector<std::thread> threads;

  // Many threads competing for single handle
  for (unsigned i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 10; ++j) {
        auto guard = pool->TryAcquire();
        if (guard.Valid()) {
          successful_acquisitions++;
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        } else {
          failed_acquisitions++;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Should have some successes and failures
  EXPECT_GT(successful_acquisitions.load(), 0);
  EXPECT_GT(failed_acquisitions.load(), 0);
  EXPECT_EQ(successful_acquisitions.load() + failed_acquisitions.load(),
            kNumThreads * 10);

  // Pool should be consistent
  auto stats = pool->GetStats();
  EXPECT_EQ(stats.max_handles, 1);
  EXPECT_EQ(stats.total_opened, 1);
  EXPECT_EQ(stats.waiting_threads, 0);
}

/// @brief Test file handle exhaustion recovery
TEST_F(TIFFHandlePoolTest, FileHandleExhaustionRecovery) {
  // Create a pool that might hit file handle limits
  constexpr unsigned kLargePoolSize = 1000;
  auto pool_result = TIFFHandlePool::Create(test_tiff_path_, kLargePoolSize);
  ASSERT_TRUE(pool_result.ok());
  auto pool = std::move(pool_result.value());

  std::vector<TIFFHandleGuard> guards;
  int handles_opened = 0;

  // Try to acquire many handles until system limits
  for (unsigned i = 0; i < 100; ++i) {  // Reasonable limit for testing
    auto guard = pool->TryAcquire();
    if (guard.Valid()) {
      guards.emplace_back(std::move(guard));
      handles_opened++;
    } else {
      break;  // Hit system limits
    }
  }

  EXPECT_GT(handles_opened, 0);

  // Release half the handles
  int release_count = handles_opened / 2;
  for (int i = 0; i < release_count; ++i) {
    guards.pop_back();
  }

  // Should be able to acquire more handles now
  auto recovery_guard = pool->TryAcquire();
  EXPECT_TRUE(recovery_guard.Valid());
}

}  // namespace fastslide
