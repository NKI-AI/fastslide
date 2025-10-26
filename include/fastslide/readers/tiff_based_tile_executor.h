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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_TILE_EXECUTOR_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fastslide {

/// @brief CRTP base class providing thread-local buffer reuse for TIFF tile executors
///
/// This class provides shared thread-local buffer management for all TIFF-based
/// tile executors (Aperio, QPTIFF, etc.). Each derived class gets its own
/// thread-local storage through template instantiation.
///
/// Benefits:
/// - Eliminates per-tile allocations (reuses buffers across tiles)
/// - Improves cache locality (buffers stay hot in L1/L2 cache)
/// - Thread-safe by design (each thread has its own buffers)
/// - Works in both sequential and parallel execution contexts
///
/// Usage:
/// ```cpp
/// class AperioTileExecutor : public TiffBasedTileExecutor<AperioTileExecutor> {
///   // Access buffers via GetBuffers().GetTileBuffer() and GetBuffers().GetCropBuffer()
/// };
/// ```
template <typename Derived>
class TiffBasedTileExecutor {
 protected:
  /// @brief Thread-local buffer pool to avoid per-tile allocations
  struct ThreadLocalTileBuffers {
    std::vector<uint8_t> tile_buffer;
    std::vector<uint8_t> crop_buffer;

    /// @brief Get or resize tile buffer to required size
    /// @param required_size Minimum buffer size needed
    /// @return Pointer to buffer with at least required_size bytes
    uint8_t* GetTileBuffer(size_t required_size) {
      if (tile_buffer.size() < required_size) {
        tile_buffer.resize(required_size);
      }
      return tile_buffer.data();
    }

    /// @brief Get or resize crop buffer to required size
    /// @param required_size Minimum buffer size needed
    /// @return Pointer to buffer with at least required_size bytes
    uint8_t* GetCropBuffer(size_t required_size) {
      if (crop_buffer.size() < required_size) {
        crop_buffer.resize(required_size);
      }
      return crop_buffer.data();
    }
  };

  /// @brief Access thread-local buffers (each derived class gets its own storage)
  static ThreadLocalTileBuffers& GetBuffers() { return tls_buffers_; }

 private:
  // Each template instantiation (Aperio, QPTIFF, etc.) gets its own thread-local storage
  static thread_local ThreadLocalTileBuffers tls_buffers_;
};

// Define the thread_local storage (in header for template)
template <typename Derived>
thread_local typename TiffBasedTileExecutor<Derived>::ThreadLocalTileBuffers
    TiffBasedTileExecutor<Derived>::tls_buffers_;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_TILE_EXECUTOR_H_
