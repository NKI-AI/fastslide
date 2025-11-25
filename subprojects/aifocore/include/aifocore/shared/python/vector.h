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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_PYTHON_VECTOR_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_PYTHON_VECTOR_H_

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>
#include "aifocore/shared/vector.h"

namespace py = pybind11;

namespace aifocore::shared::python {

// A capsule that decrements the reference count when the array is destroyed
struct CapsuleData {
  size_t index;
  bi::interprocess_mutex* mutex;
  SharedChunksVector* data_chunks_;

  CapsuleData(size_t idx, bi::interprocess_mutex* mtx, SharedChunksVector* dc)
      : index(idx), mutex(mtx), data_chunks_(dc) {}
};

template <typename T>
py::array_t<T> GetPython(SharedVector& vector, size_t index) {
  // Get the shared_ptr to the chunk
  auto chunk_ptr = vector.GetChunk(index);

  // Extract shape and strides
  std::vector<size_t> shape(chunk_ptr->shape->begin(), chunk_ptr->shape->end());
  std::vector<size_t> strides(shape.size());
  size_t stride = sizeof(T);
  for (ssize_t i = shape.size() - 1; i >= 0; --i) {
    strides[i] = stride;
    stride *= shape[i];
  }

  // Create capsule to manage reference count on deletion
  // Store the shared_ptr in the capsule
  auto capsule =
      py::capsule(new std::shared_ptr<SharedChunk>(chunk_ptr), [](void* ptr) {
        // The deleter will be called when the capsule is destroyed
        auto chunk_ptr = static_cast<std::shared_ptr<SharedChunk>*>(ptr);
        delete chunk_ptr;  // This will decrement the ref_count via
                           // shared_ptr's destructor
      });

  // Return py::array_t that provides a view into the shared memory
  return py::array_t<T>(shape, strides, chunk_ptr->data->data(), capsule);
}

template <typename T>
void AppendPython(SharedVector& vector, py::array_t<T> array) {
  py::buffer_info info = array.request();

  if (!(array.flags() & py::array::c_style)) {
    throw std::runtime_error(
        "Array is not contiguous. Only contiguous arrays are supported for "
        "appending.");
  }

  size_t data_size = info.size;
  const T* data_ptr = static_cast<const T*>(info.ptr);
  std::vector<size_t> shape(info.shape.begin(), info.shape.end());

  // Convert data to float if necessary
  std::vector<float> data_converted(data_size);
  if constexpr (std::is_same_v<T, float>) {
    // If T is float, we can copy directly
    std::copy(data_ptr, data_ptr + data_size, data_converted.begin());
  } else {
    // Convert each element to float
    std::transform(data_ptr, data_ptr + data_size, data_converted.begin(),
                   [](T val) { return static_cast<float>(val); });
  }

  // Call the append method of SharedVector with the converted data and shape
  vector.append(data_converted, shape);
}

template <typename T>
void ReplacePython(SharedVector& vector, size_t index, py::array_t<T> array) {
  py::buffer_info info = array.request();

  if (!(array.flags() & py::array::c_style)) {
    throw std::runtime_error(
        "Array is not contiguous. Only contiguous arrays are supported for "
        "replacing.");
  }

  size_t data_size = info.size;
  const T* data_ptr = static_cast<const T*>(info.ptr);
  std::vector<size_t> shape(info.shape.begin(), info.shape.end());

  // Convert data to float if necessary
  std::vector<float> data_converted(data_size);
  if constexpr (std::is_same_v<T, float>) {
    // If T is float, we can copy directly
    std::copy(data_ptr, data_ptr + data_size, data_converted.begin());
  } else {
    // Convert each element to float
    std::transform(data_ptr, data_ptr + data_size, data_converted.begin(),
                   [](T val) { return static_cast<float>(val); });
  }

  // Call the replace method of SharedVector with the converted data and shape
  vector.replace(index, data_converted, shape);
}

}  // namespace aifocore::shared::python

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_PYTHON_VECTOR_H_
