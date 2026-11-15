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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_INFLIGHT_DECODES_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_INFLIGHT_DECODES_H_

#include <array>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "fastslide/runtime/cache_interface.h"

namespace fastslide {
namespace readers {

/// @brief Decodes in progress, so that concurrent cache misses on the same tile
/// decode it once.
///
/// The first thread to miss on a key becomes its leader and decodes the tile;
/// threads missing on the same key meanwhile wait for the leader's result
/// instead of decoding it again. Process-wide, so readers sharing the global
/// cache share their decodes too (the key carries the file name).
class InflightDecodes {
 public:
  using Tile = std::shared_ptr<runtime::CachedTileData>;

  static InflightDecodes& Instance() {
    static InflightDecodes instance;
    return instance;
  }

  /// @brief Waiters on one decode. Only allocated when a second thread misses
  /// on a key already being decoded, so an uncontended miss costs one map
  /// insert and erase.
  class Waiters {
   public:
    /// @brief Block until the leader finishes; null if its decode failed.
    Tile Wait() {
      std::unique_lock<std::mutex> lock(mutex_);
      done_cv_.wait(lock, [this] { return done_; });
      return tile_;
    }

   private:
    friend class InflightDecodes;

    void Wake(Tile tile) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        tile_ = std::move(tile);
        done_ = true;
      }
      done_cv_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable done_cv_;
    bool done_ = false;
    Tile tile_;
  };

  /// @brief Result of `Join`: lead the decode of the key (`waiters` null; then
  /// `Finish` it exactly once, see `Leader`), or wait on `waiters`.
  struct Claim {
    bool leader = false;
    std::shared_ptr<Waiters> waiters;
  };

  /// @brief Lead the decode of `key`, or join the decode already in flight.
  Claim Join(const runtime::TileKey& key) {
    Shard& shard = ShardOf(key);
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto [it, inserted] = shard.inflight.try_emplace(key);
    if (inserted) {
      return Claim{true, nullptr};
    }
    if (!it->second) {
      it->second = std::make_shared<Waiters>();
    }
    return Claim{false, it->second};
  }

  /// @brief End the decode of `key`: wake its waiters with `tile` (null if the
  /// decode failed; they then decode the tile themselves).
  void Finish(const runtime::TileKey& key, Tile tile) {
    std::shared_ptr<Waiters> waiters;
    {
      Shard& shard = ShardOf(key);
      std::lock_guard<std::mutex> lock(shard.mutex);
      auto it = shard.inflight.find(key);
      if (it == shard.inflight.end()) {
        return;
      }
      waiters = std::move(it->second);
      shard.inflight.erase(it);
    }
    if (waiters) {
      waiters->Wake(std::move(tile));
    }
  }

  /// @brief Number of decodes in flight (for tests).
  [[nodiscard]] std::size_t Size() const {
    std::size_t size = 0;
    for (const Shard& shard : shards_) {
      std::lock_guard<std::mutex> lock(shard.mutex);
      size += shard.inflight.size();
    }
    return size;
  }

  /// @brief Finishes the decode of a key on scope exit, with a null tile unless
  /// `Finish` was called, so waiters never hang on an error path.
  class Leader {
   public:
    explicit Leader(runtime::TileKey key) : key_(std::move(key)) {}

    Leader(const Leader&) = delete;
    Leader& operator=(const Leader&) = delete;

    ~Leader() {
      if (!finished_) {
        Instance().Finish(key_, nullptr);
      }
    }

    void Finish(Tile tile) {
      finished_ = true;
      Instance().Finish(key_, std::move(tile));
    }

   private:
    runtime::TileKey key_;
    bool finished_ = false;
  };

 private:
  struct KeyHash {
    std::size_t operator()(const runtime::TileKey& key) const {
      std::size_t h = std::hash<std::string>{}(key.filename);
      for (const std::size_t v :
           {std::size_t{key.level}, std::size_t{key.tile_x},
            std::size_t{key.tile_y}}) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      }
      return h;
    }
  };

  // Sharded by key so concurrent misses on different tiles rarely contend.
  static constexpr std::size_t kShards = 16;

  struct Shard {
    mutable std::mutex mutex;
    // Value is null until a second thread waits on the key.
    std::unordered_map<runtime::TileKey, std::shared_ptr<Waiters>, KeyHash>
        inflight;
  };

  Shard& ShardOf(const runtime::TileKey& key) {
    return shards_[KeyHash{}(key) % kShards];
  }

  std::array<Shard, kShards> shards_;
};

}  // namespace readers
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_INFLIGHT_DECODES_H_
