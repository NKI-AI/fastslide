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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_TILING_PYTHON_GRID_WRAPPER_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_TILING_PYTHON_GRID_WRAPPER_H_
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <algorithm>
#include <string>
#include <variant>
#include <vector>
#include "aifocore/tiling/grid.h"

namespace py = pybind11;
using aifocore::tiling::Grid;
using aifocore::tiling::GridOrder;

namespace aifocore::tiling::python {

// Helper function to convert py::object to TilingMode
TilingMode ParseTilingMode(const py::object& mode_obj) {
  if (py::isinstance<py::str>(mode_obj)) {
    std::string mode_str = mode_obj.cast<std::string>();
    if (mode_str == "skip") {
      return TilingMode::kSkip;
    } else if (mode_str == "overflow") {
      return TilingMode::kOverflow;
    } else {
      throw std::invalid_argument("Invalid tiling mode: " + mode_str);
    }
  } else if (py::isinstance<TilingMode>(mode_obj)) {
    return mode_obj.cast<TilingMode>();
  } else {
    throw std::invalid_argument(
        "Invalid type for tiling mode. Expected a string or TilingMode enum.");
  }
}

// Helper function to convert py::object to GridOrder
GridOrder ParseGridOrder(const py::object& order_obj) {
  if (py::isinstance<py::str>(order_obj)) {
    std::string order_str = order_obj.cast<std::string>();
    if (order_str == "C") {
      return GridOrder::kC;
    } else if (order_str == "F") {
      return GridOrder::kF;
    } else {
      throw std::invalid_argument("Invalid grid order: " + order_str);
    }
  } else if (py::isinstance<GridOrder>(order_obj)) {
    return order_obj.cast<GridOrder>();
  } else {
    throw std::invalid_argument(
        "Invalid type for grid order. Expected a string or GridOrder enum.");
  }
}

// Helper function to check if any element in a py::object is a float
static bool ContainsFloat(const py::handle& obj) {
  if (py::isinstance<py::sequence>(obj)) {
    for (const auto& item : obj) {
      if (py::isinstance<py::float_>(item)) {
        return true;
      } else if (py::isinstance<py::sequence>(item)) {
        // Recursively check nested sequences
        if (ContainsFloat(item)) {
          return true;
        }
      }
    }
  }
  return py::isinstance<py::float_>(obj);
}

// Helper function to check if all elements in a py::object are integers
static bool IsAllIntegers(const py::handle& obj) {
  if (py::isinstance<py::sequence>(obj)) {
    return std::ranges::all_of(obj, [](const py::handle& item) {
      return py::isinstance<py::int_>(item) ||
             (py::isinstance<py::sequence>(item) && IsAllIntegers(item));
    });
  }
  return py::isinstance<py::int_>(obj);
}

template <typename T>
std::vector<T> ConvertToVector(const py::handle& obj) {
  if (py::isinstance<py::sequence>(obj)) {
    return obj.cast<std::vector<T>>();
  } else {
    // Single value - convert to length-1 vector
    return {obj.cast<T>()};
  }
}

// Wrapper function to choose integer or double implementation
py::list TilesGridCoordinatesWrapper(const py::object& size,
                                     const py::object& tile_size,
                                     const py::object& tile_overlap,
                                     const py::object& mode_obj) {
  TilingMode mode = ParseTilingMode(mode_obj);

  // Determine if all inputs are integers
  bool use_int = IsAllIntegers(size) && IsAllIntegers(tile_size) &&
                 IsAllIntegers(tile_overlap);

  if (use_int) {
    // Convert to std::vector<int> with flexible input handling
    std::vector<int> size_int = ConvertToVector<int>(size);
    std::vector<int> tile_size_int = ConvertToVector<int>(tile_size);
    std::vector<int> tile_overlap_int = ConvertToVector<int>(tile_overlap);

    // Broadcast scalar inputs to match the longest dimension
    size_t max_dim = std::max(
        {size_int.size(), tile_size_int.size(), tile_overlap_int.size()});
    if (size_int.size() == 1) {
      size_int.resize(max_dim, size_int[0]);
    }
    if (tile_size_int.size() == 1) {
      tile_size_int.resize(max_dim, tile_size_int[0]);
    }
    if (tile_overlap_int.size() == 1) {
      tile_overlap_int.resize(max_dim, tile_overlap_int[0]);
    }

    // Call TilesGridCoordinates with expanded inputs
    auto coordinates = TilesGridCoordinates<int>(size_int, tile_size_int,
                                                 tile_overlap_int, mode);
    return py::cast(coordinates);
  } else {
    // Convert to std::vector<double> with flexible input handling
    std::vector<double> size_double = ConvertToVector<double>(size);
    std::vector<double> tile_size_double = ConvertToVector<double>(tile_size);
    std::vector<double> tile_overlap_double =
        ConvertToVector<double>(tile_overlap);

    // Broadcast scalar inputs to match the longest dimension
    size_t max_dim = std::max({size_double.size(), tile_size_double.size(),
                               tile_overlap_double.size()});
    if (size_double.size() == 1) {
      size_double.resize(max_dim, size_double[0]);
    }
    if (tile_size_double.size() == 1) {
      tile_size_double.resize(max_dim, tile_size_double[0]);
    }
    if (tile_overlap_double.size() == 1) {
      tile_overlap_double.resize(max_dim, tile_overlap_double[0]);
    }

    // Call TilesGridCoordinates with expanded inputs
    auto coordinates = TilesGridCoordinates<double>(
        size_double, tile_size_double, tile_overlap_double, mode);
    return py::cast(coordinates);
  }
}

// GridWrapper class that holds either Grid<int> or Grid<double>
class GridWrapper {
 public:
  // Constructors
  explicit GridWrapper(const Grid<int>& grid_int) : grid_(grid_int) {}

  explicit GridWrapper(const Grid<double>& grid_double) : grid_(grid_double) {}

  GridWrapper(const py::list& coordinates, const py::object& order_obj)
      : grid_([&coordinates,
               &order_obj]() -> std::variant<Grid<int>, Grid<double>> {
          // Convert order_obj to GridOrder
          GridOrder order = ParseGridOrder(order_obj);

          // Detect if we should use int or double
          bool use_double = false;
          // We'll store intermediate coordinates as double for convenience
          std::vector<std::vector<double>> double_coordinates;

          for (auto& item : coordinates) {
            if (!py::isinstance<py::sequence>(item)) {
              throw std::invalid_argument(
                  "Each element of coordinates must be a sequence.");
            }

            // Check if this sequence contains any floats
            if (ContainsFloat(item)) {
              use_double = true;
            }

            // Cast directly to double for simplicity
            std::vector<double> coord_row = item.cast<std::vector<double>>();
            double_coordinates.push_back(coord_row);
          }

          if (use_double) {
            // Construct a double-based Grid
            return Grid<double>(double_coordinates, order);
          } else {
            // Convert to int-based Grid if all integers
            std::vector<std::vector<int>> int_coordinates;
            for (auto& coord_row : double_coordinates) {
              // Convert each double row to int row
              int_coordinates.emplace_back(coord_row.begin(), coord_row.end());
            }
            return Grid<int>(int_coordinates, order);
          }
        }()) {}

  // Static method to create from tiling
  static GridWrapper FromTiling(const py::object& offset_obj,
                                const py::object& size_obj,
                                const py::object& tile_size_obj,
                                const py::object& tile_overlap_obj,
                                const py::object& mode_obj,
                                const py::object& order_obj) {
    TilingMode mode = ParseTilingMode(mode_obj);
    GridOrder order = ParseGridOrder(order_obj);

    // Determine if all inputs are integers
    bool use_int = IsAllIntegers(offset_obj) && IsAllIntegers(size_obj) &&
                   IsAllIntegers(tile_size_obj) &&
                   IsAllIntegers(tile_overlap_obj);

    if (use_int) {
      // Convert to std::vector<int>
      std::vector<int> offset = offset_obj.cast<std::vector<int>>();
      std::vector<int> size = size_obj.cast<std::vector<int>>();
      std::vector<int> tile_size = tile_size_obj.cast<std::vector<int>>();
      std::vector<int> tile_overlap = tile_overlap_obj.cast<std::vector<int>>();

      Grid<int> grid = Grid<int>::FromTiling(offset, size, tile_size,
                                             tile_overlap, mode, order);
      return GridWrapper(grid);
    } else {
      // Convert to std::vector<double>
      std::vector<double> offset = offset_obj.cast<std::vector<double>>();
      std::vector<double> size = size_obj.cast<std::vector<double>>();
      std::vector<double> tile_size = tile_size_obj.cast<std::vector<double>>();
      std::vector<double> tile_overlap =
          tile_overlap_obj.cast<std::vector<double>>();

      Grid<double> grid = Grid<double>::FromTiling(offset, size, tile_size,
                                                   tile_overlap, mode, order);
      return GridWrapper(grid);
    }
  }

  // Access the grid size
  py::tuple Size() const {
    return std::visit(
        [](auto&& grid) {
          std::vector<int> sz = grid.Size();
          return py::cast(sz);
        },
        grid_);
  }

  // Get the length of the grid
  size_t Length() const {
    return std::visit([](auto&& grid) { return grid.Length(); }, grid_);
  }

  // Get the grid order
  GridOrder Order() const {
    return std::visit([](auto&& grid) { return grid.Order(); }, grid_);
  }

  const std::variant<Grid<int>, Grid<double>>& GetGrid() const { return grid_; }

  // Get an item from the grid
  py::tuple GetItem(size_t index) const {
    return std::visit(
        [index](auto&& grid) {
          auto point = grid[index];
          return py::cast(point);
        },
        grid_);
  }

  // Iterator implementation
  class iterator {
   public:
    iterator(const GridWrapper& grid_wrapper, size_t index)
        : grid_wrapper_(grid_wrapper), index_(index) {}

    iterator& operator++() {
      ++index_;
      return *this;
    }

    py::object operator*() const { return grid_wrapper_.GetItem(index_); }

    bool operator!=(const iterator& other) const {
      return index_ != other.index_;
    }

    bool operator==(const iterator& other) const {
      return index_ == other.index_;
    }

   private:
    const GridWrapper& grid_wrapper_;
    size_t index_;
  };

  iterator begin() const { return iterator(*this, 0); }

  iterator end() const { return iterator(*this, Length()); }

 private:
  std::variant<Grid<int>, Grid<double>> grid_;

  // Helper function to check if all elements in a py::object are integers
  static bool IsAllIntegers(const py::handle& obj) {
    if (py::isinstance<py::sequence>(obj)) {
      for (const auto& item : obj) {
        if (py::isinstance<py::int_>(item)) {
          continue;
        } else if (py::isinstance<py::sequence>(item)) {
          if (!IsAllIntegers(item)) {
            return false;
          }
        } else {
          return false;
        }
      }
      return true;
    } else {
      return py::isinstance<py::int_>(obj);
    }
  }
};

}  // namespace aifocore::tiling::python

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_TILING_PYTHON_GRID_WRAPPER_H_
