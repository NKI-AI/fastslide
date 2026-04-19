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

/// @file ndarray_numpy.h
/// @brief Zero-copy conversion utilities between NDArrayView and numpy arrays.
///
/// This file provides utilities to create numpy arrays from NDArrayView with
/// zero-copy semantics and proper lifetime management through parent object
/// keep-alive.
///
/// SAFETY:
/// - The parent object must keep the underlying data alive for the lifetime
///   of the numpy array.
/// - Use py::capsule with appropriate destructor to manage parent lifetime.
/// - NDArrayView must point to contiguous row-major memory.
///
/// EXAMPLE:
///   // Transfer ownership of std::vector to Python
///   auto* heap_vec = new std::vector<double>({1.0, 2.0, 3.0, 4.0});
///   NDArrayView<double, 2> view(heap_vec->data(), {2, 2});
///   py::capsule parent(heap_vec, [](void* p) {
///     delete static_cast<std::vector<double>*>(p);
///   });
///   py::array_t<double> arr = ToNumpy(view, parent);
///   // When Python's arr is garbage collected, heap_vec is deleted
///
/// EXAMPLE:
///   // Keep alive a py::object that owns the data
///   py::array_t<double> source = ...;
///   NDArrayView<const double, 2> view(source.data(), {rows, cols});
///   py::array_t<double> arr = ToNumpy(view, source);
///   // source is kept alive as long as arr exists

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_PYTHON_NDARRAY_NUMPY_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_PYTHON_NDARRAY_NUMPY_H_

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "aifocore/math/ndarray.h"

namespace py = pybind11;

namespace aifocore::math::python {

/// @brief Convert NDArrayView to numpy array with zero-copy and parent
/// keep-alive.
///
/// Creates a numpy array that shares memory with the NDArrayView. The parent
/// object is kept alive through a capsule base object, ensuring the underlying
/// data remains valid for the lifetime of the numpy array.
///
/// @tparam T Element type (can be const-qualified).
/// @tparam N Number of dimensions.
/// @param view Non-owning view over contiguous row-major data.
/// @param parent Python object or capsule that owns/manages the data lifetime.
///               This can be a py::capsule, py::array, or any py::object.
///               The parent will be kept alive as long as the returned array
///               exists.
/// @return py::array_t<T> that provides zero-copy view into view's data.
///
/// @note The view must point to contiguous row-major memory (enforced by
///       NDArrayView).
/// @note For const T, returns py::array_t<const T> which is read-only from
///       Python.
template <typename T, std::size_t N>
py::array_t<T> ToNumpy(const NDArrayView<T, N>& view, py::handle parent) {
  using value_type = std::remove_const_t<T>;
  static_assert(N > 0, "NDArrayView dimension must be positive");

  const auto& shape = view.Shape();
  const auto& strides = view.Strides();

  // Convert shape and element-based strides to numpy format
  std::vector<std::size_t> numpy_shape(shape.begin(), shape.end());
  std::vector<std::size_t> numpy_strides(N);

  // NDArrayView strides are element-based, numpy needs byte strides
  for (std::size_t i = 0; i < N; ++i) {
    numpy_strides[i] = strides[i] * sizeof(value_type);
  }

  // Get raw pointer (const or non-const depending on T)
  T* data_ptr = const_cast<T*>(view.Data());

  // Create numpy array with parent as base object for lifetime management
  // The parent object's refcount is incremented and will be decremented when
  // the numpy array is destroyed
  return py::array_t<T>(numpy_shape, numpy_strides, data_ptr, parent);
}

/// @brief Overload for py::object parent (convenience).
template <typename T, std::size_t N>
py::array_t<T> ToNumpy(const NDArrayView<T, N>& view,
                       const py::object& parent) {
  return ToNumpy(view, py::handle(parent));
}

/// @brief Overload for py::capsule parent (convenience).
template <typename T, std::size_t N>
py::array_t<T> ToNumpy(const NDArrayView<T, N>& view,
                       const py::capsule& parent) {
  return ToNumpy(view, py::handle(parent));
}

/// @brief Convert std::vector<std::pair<T, T>> to numpy array with ownership
/// transfer.
///
/// This overload simplifies the common pattern of converting a vector of pairs
/// to a 2D numpy array by handling the ownership transfer, capsule creation,
/// and view setup automatically.
///
/// @tparam T Element type.
/// @param vec Vector of pairs to convert (moved to heap for Python ownership).
/// @return py::array_t<T> of shape (N, 2) that owns the data.
///
/// @note The vector is moved to the heap and will be deleted when the numpy
///       array is garbage collected.
///
/// EXAMPLE:
///   std::vector<std::pair<double, double>> points = {{1.0, 2.0}, {3.0, 4.0}};
///   py::array_t<double> arr = ToNumpy(std::move(points));
///   // arr has shape (2, 2), points is now empty
template <typename T>
py::array_t<T> ToNumpy(std::vector<std::pair<T, T>>&& vec) {
  // Transfer ownership to heap
  auto* heap_data = new std::vector<std::pair<T, T>>(std::move(vec));
  const std::size_t num_points = heap_data->size();

  // Create capsule that will delete the vector when Python is done
  py::capsule free_when_done(heap_data, [](void* ptr) {
    delete static_cast<std::vector<std::pair<T, T>>*>(ptr);
  });

  // std::pair<T, T> has the same memory layout as two consecutive T values,
  // so we can safely reinterpret the data as a contiguous (N, 2) array
  NDArrayView<T, 2> view(reinterpret_cast<T*>(heap_data->data()),
                         {num_points, 2});

  return ToNumpy(view, free_when_done);
}

}  // namespace aifocore::math::python

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_PYTHON_NDARRAY_NUMPY_H_
