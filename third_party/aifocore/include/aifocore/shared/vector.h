// Copyright 2024 Jonas Teuwen. All Rights Reserved.
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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_VECTOR_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_VECTOR_H_

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/atomic.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

#include "aifocore/shared/exceptions.h"

namespace bi = boost::interprocess;

namespace aifocore::shared {

using ShmemMutex = bi::interprocess_mutex;
using ScopedLock = bi::scoped_lock<ShmemMutex>;

using ShmemAllocator =
    bi::allocator<float, bi::managed_shared_memory::segment_manager>;
using SharedVectorData = bi::vector<float, ShmemAllocator>;
using SizeTAllocator =
    bi::allocator<std::size_t, bi::managed_shared_memory::segment_manager>;
using SharedSizeVector = bi::vector<std::size_t, SizeTAllocator>;

struct SharedChunk {
  bi::offset_ptr<SharedVectorData> data;
  bi::offset_ptr<SharedSizeVector> shape;
  boost::atomic<std::size_t> ref_count;
  ShmemMutex write_lock;

  SharedChunk(bi::offset_ptr<SharedVectorData> d,
              bi::offset_ptr<SharedSizeVector> s)
      : data(d), shape(s), ref_count(0) {}

  SharedChunk(const SharedChunk&) = delete;
  SharedChunk& operator=(const SharedChunk&) = delete;
  SharedChunk(SharedChunk&&) = delete;
  SharedChunk& operator=(SharedChunk&&) = delete;
};

struct SharedChunkDeleter {
  void operator()(SharedChunk* chunk) const {
    if (chunk) {
      chunk->ref_count.fetch_sub(1, boost::memory_order_acq_rel);
    }
  }
};

using SharedChunkPtr = bi::offset_ptr<SharedChunk>;
using SharedChunkPtrAllocator =
    bi::allocator<SharedChunkPtr, bi::managed_shared_memory::segment_manager>;
using SharedChunksVector = bi::vector<SharedChunkPtr, SharedChunkPtrAllocator>;

struct SharedResources {
  bi::managed_shared_memory segment;
  ShmemAllocator alloc_inst;
  SizeTAllocator size_t_alloc_inst;
  bi::allocator<SharedChunk, bi::managed_shared_memory::segment_manager>
      chunk_alloc_inst;
  ShmemMutex& mutex;
  boost::atomic<std::size_t>* ref_count;
  // lock-free public size accessor
  boost::atomic<std::size_t>* logical_size;

  SharedResources(const std::string& name, std::size_t max_memory_size)
      : segment(bi::open_or_create, name.c_str(), max_memory_size),
        alloc_inst(segment.get_segment_manager()),
        size_t_alloc_inst(segment.get_segment_manager()),
        chunk_alloc_inst(segment.get_segment_manager()),
        mutex(*segment.find_or_construct<ShmemMutex>("mutex")()) {
    ref_count =
        segment.find_or_construct<boost::atomic<std::size_t>>("ref_count")(0);
    logical_size = segment.find_or_construct<boost::atomic<std::size_t>>(
        "logical_size")(0);
    bool is_first_instance = ref_count->load(boost::memory_order_acquire) == 0;
    ref_count->fetch_add(1, boost::memory_order_acq_rel);

    if (is_first_instance) {
      // You can log here if it's the first instance
    } else {
      // You can log here if it's not the first instance
    }
  }

  // This is called when the shared resources object is destroyed. This will
  // reduce the reference count. This reference count is used to ensure that if
  // there are multiple (possibly independent) processes are ran, the shared
  // resources stay alive
  ~SharedResources() { ref_count->fetch_sub(1, boost::memory_order_acq_rel); }
};

class SharedVector {
 public:
  SharedVector(const std::string& name,
               std::size_t chunk_size = 1024 * 1024 * 1 /*1MB*/,
               std::size_t max_memory_size = 1024 * 1024 * 10)
      : name_(name),
        chunk_size_(chunk_size),
        shared_resources_(
            std::make_unique<SharedResources>(name, max_memory_size)) {
    ScopedLock lock(shared_resources_->mutex);

    // Check if data_chunks_ already exists in the shared memory segment
    data_chunks_ =
        shared_resources_->segment.find<SharedChunksVector>("data_chunks")
            .first;
    if (!data_chunks_) {
      // If it does not exist, create a new one
      data_chunks_ = shared_resources_->segment.construct<SharedChunksVector>(
          "data_chunks")(shared_resources_->segment.get_segment_manager());
    }

    std::size_t max_chunks = max_memory_size / chunk_size_;
    data_chunks_->reserve(max_chunks);

    shared_resources_->logical_size->store(data_chunks_->size(),
                                           boost::memory_order_relaxed);

    // You could log here for instance the shared memory base address using
    // shared_resources_->segment.get_address();
  }

  ~SharedVector() {
    ScopedLock lock(shared_resources_->mutex);
    // The current ref count can be obtained with
    // shared_resources_->ref_count->load(boost::memory_order_relaxed);
  }

  SharedResources& GetSharedResources() { return *shared_resources_; }

  SharedChunksVector* GetDataChunks() const { return data_chunks_; }

  std::shared_ptr<SharedChunk> GetChunk(std::size_t index) {
    if (index >=
        shared_resources_->logical_size->load(boost::memory_order_acquire)) {
      throw std::out_of_range("Index out of range");
    }

    SharedChunk* raw = (*data_chunks_)[index].get();
    raw->ref_count.fetch_add(1, boost::memory_order_acq_rel);

    return std::shared_ptr<SharedChunk>(raw, SharedChunkDeleter());
  }

  template <typename T>
  void append(const std::vector<T>& data,
              const std::vector<std::size_t>& shape) {
    std::size_t data_size = data.size();

    if (data_size * sizeof(T) > chunk_size_) {
      throw exceptions::MemoryError("Data size exceeds chunk capacity.");
    }

    ScopedLock lock(shared_resources_->mutex);

    if (shared_resources_->segment.get_free_memory() <
        chunk_size_ + 256) {  // Add 256 as a small overhead
      throw exceptions::OutOfMemoryError("Not enough memory to append array.");
    }

    SharedVectorData* shared_data =
        shared_resources_->segment.construct<SharedVectorData>(
            bi::anonymous_instance)(shared_resources_->alloc_inst);

    // Reserve chunk size and assign data
    shared_data->reserve(chunk_size_ / sizeof(T));
    shared_data->assign(data.begin(), data.end());

    // Construct the shared shape vector
    SharedSizeVector* shared_shape =
        shared_resources_->segment.construct<SharedSizeVector>(
            bi::anonymous_instance)(shared_resources_->size_t_alloc_inst);

    shared_shape->assign(shape.begin(), shape.end());

    // Create a new SharedChunk and store it
    SharedChunk* new_chunk = shared_resources_->segment.construct<SharedChunk>(
        bi::anonymous_instance)(shared_data, shared_shape);
    if (data_chunks_->size() >= data_chunks_->capacity()) {
      throw std::runtime_error("Data chunks vector is full");
    }
    data_chunks_->emplace_back(new_chunk);
    shared_resources_->logical_size->store(data_chunks_->size(),
                                           boost::memory_order_release);
  }

  template <typename T>
  void replace(std::size_t index, const std::vector<T>& data,
               const std::vector<std::size_t>& shape) {
    std::size_t data_size = data.size();
    if (data_size * sizeof(T) > chunk_size_) {
      throw exceptions::MemoryError("Array size exceeds chunk size");
    }

    if (index >=
        shared_resources_->logical_size->load(boost::memory_order_acquire)) {
      throw std::out_of_range("Index out of range");
    }

    SharedChunk* chunk = (*data_chunks_)[index].get();
    ScopedLock lock(chunk->write_lock);

    if (chunk->ref_count.load(boost::memory_order_acquire) > 0) {
      throw std::runtime_error("Cannot replace chunk because it is in use");
    }

    // Replace the data and shape in the chunk
    // chunk->data->resize(data_size);
    chunk->data->assign(data.begin(), data.end());
    chunk->shape->assign(shape.begin(), shape.end());
  }

  std::size_t size() const noexcept {
    return shared_resources_->logical_size->load(boost::memory_order_acquire);
  }

  std::size_t GetChunkRefCount(std::size_t index) {
    if (index >=
        shared_resources_->logical_size->load(boost::memory_order_acquire)) {
      throw std::out_of_range("Index out of range");
    }
    const SharedChunkPtr& chunk_ptr = (*data_chunks_)[index];
    if (!chunk_ptr) {
      throw std::runtime_error("Chunk pointer is null");
    }
    const SharedChunk& chunk = *chunk_ptr;
    return chunk.ref_count.load(boost::memory_order_acquire);
  }

  std::size_t GetChunkPointer(std::size_t index) {
    if (index >=
        shared_resources_->logical_size->load(boost::memory_order_acquire)) {
      throw std::out_of_range("Index out of range");
    }
    const SharedChunkPtr& chunk_ptr = (*data_chunks_)[index];
    if (!chunk_ptr || !chunk_ptr->data) {
      throw std::runtime_error("Chunk pointer or data is null");
    }
    return reinterpret_cast<std::size_t>(chunk_ptr->data->data());
  }

  std::vector<std::size_t> GetChunkShape(std::size_t index) const {
    if (index >=
        shared_resources_->logical_size->load(boost::memory_order_acquire)) {
      throw std::out_of_range("Index out of range");
    }
    const SharedChunkPtr& chunk_ptr = (*data_chunks_)[index];
    if (!chunk_ptr || !chunk_ptr->shape) {
      throw std::runtime_error("Chunk pointer or shape is null");
    }
    const SharedSizeVector& shape = *chunk_ptr->shape;
    return std::vector<std::size_t>(shape.begin(), shape.end());
  }

  std::size_t GetRefCount() const {
    return shared_resources_->ref_count->load(boost::memory_order_acquire);
  }

  std::size_t GetFreeMemory() const {
    return shared_resources_->segment.get_free_memory();
  }

 private:
  std::string name_;
  std::size_t chunk_size_;
  std::unique_ptr<SharedResources> shared_resources_;
  SharedChunksVector* data_chunks_;
};

}  // namespace aifocore::shared

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_VECTOR_H_
