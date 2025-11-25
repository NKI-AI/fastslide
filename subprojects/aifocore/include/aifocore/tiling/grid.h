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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_TILING_GRID_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_TILING_GRID_H_

#include <aifocore/concepts/numeric.h>
#include <aifocore/ranges/zip.h>
#include <algorithm>
#include <cmath>
#include <compare>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace aifocore::tiling {

enum class TilingMode {
  kSkip,
  kOverflow,
};

enum class GridOrder {
  kC,
  kF,
};

constexpr bool operator==(GridOrder lhs, GridOrder rhs) {
  return static_cast<std::underlying_type_t<GridOrder>>(lhs) ==
         static_cast<std::underlying_type_t<GridOrder>>(rhs);
}

constexpr bool operator!=(GridOrder lhs, GridOrder rhs) {
  return !(lhs == rhs);
}

template <aifocore::GenericNumber T>
std::vector<std::vector<T>> TilesGridCoordinates(
    const std::vector<T>& size, const std::vector<T>& tile_size,
    const std::vector<T>& tile_overlap, TilingMode mode) {

  // Validate dimensions match
  const size_t dim = size.size();
  if (dim != tile_size.size() || dim != tile_overlap.size()) {
    throw std::invalid_argument(
        "size, tile_size, and tile_overlap must have the same dimensions.");
  }

  // Validate positive values
  if (std::ranges::any_of(size, [](T val) { return val <= 0; }) ||
      std::ranges::any_of(tile_size, [](T val) { return val <= 0; })) {
    throw std::invalid_argument(
        "size and tile_size must be greater than zero.");
  }

  // Calculate adjusted overlap using custom zip implementation
  // (clang doesn't support std::ranges::zip)
  std::vector<T> adjusted_overlap(dim);
  for (auto&& [overlap, t_size, s_size, adjusted] :
       aifocore::ranges::zip(tile_overlap, tile_size, size, adjusted_overlap)) {
    adjusted = std::fmod(overlap, std::min(t_size, s_size));
    if (adjusted < 0) {
      adjusted += std::min(t_size, s_size);
    }
  }

  // Calculate stride
  std::vector<T> stride(dim);
  for (size_t i = 0; i < dim; ++i) {
    stride[i] = tile_size[i] - adjusted_overlap[i];
  }

  // Calculate tiles info
  std::vector<int> num_tiles(dim);
  std::vector<double> tiled_size(dim);
  std::vector<double> overflow(dim, 0.0);

  for (size_t i = 0; i < dim; ++i) {
    const double num_tiles_double =
        (static_cast<double>(size[i] - tile_size[i]) /
         static_cast<double>(stride[i])) +
        1.0;

    if (mode == TilingMode::kOverflow) {
      num_tiles[i] = static_cast<int>(std::ceil(num_tiles_double));
      tiled_size[i] = (num_tiles[i] - 1) * stride[i] + tile_size[i];
      overflow[i] = tiled_size[i] - size[i];
    } else {
      num_tiles[i] = static_cast<int>(std::floor(num_tiles_double));
    }
  }

  std::vector<std::vector<T>> coordinates(dim);
  // Use aifocore::ranges::zip to iterate over num_tiles, stride, and
  // coordinates together
  for (auto&& [n, dstride, coord] :
       aifocore::ranges::zip(num_tiles, stride, coordinates)) {
    // Generate the sequence using iota and transform, then assign to coord
    auto range = std::ranges::iota_view(0, n) |
                 std::ranges::views::transform(
                     [dstride](int j) { return static_cast<T>(j) * dstride; });
    coord.assign(range.begin(), range.end());
  }

  return coordinates;
}

template <aifocore::GenericNumber T>
class Grid {
 public:
  using Coordinate = std::vector<T>;
  using Coordinates = std::vector<Coordinate>;

  // Constructor for both regular and irregular grids
  Grid(const Coordinates& coordinates, GridOrder order)
      : coordinates_(coordinates), order_(order), is_irregular_(false) {}

  // Constructor specifically for irregular grids
  Grid(const std::vector<Coordinate>& points, GridOrder order,
       bool is_irregular)
      : order_(order), is_irregular_(is_irregular) {
    if (is_irregular_) {
      // Flatten irregular grid points into a single set of coordinates
      irregular_coordinates_ = points;
    } else {
      coordinates_ = points;
    }
  }

  // Factory method to create Grid from tiling parameters
  static Grid FromTiling(const std::vector<T>& offset,
                         const std::vector<T>& size,
                         const std::vector<T>& tile_size,
                         const std::vector<T>& tile_overlap, TilingMode mode,
                         GridOrder order) {
    auto coordinates =
        TilesGridCoordinates(size, tile_size, tile_overlap, mode);
    ApplyOffset(coordinates, offset);
    return Grid(coordinates, order);
  }

  // Factory method to create Grid from a list of irregular points
  static Grid FromCoordinates(const std::vector<std::vector<T>>& points,
                              GridOrder order) {
    if (points.empty()) {
      return Grid({}, order, true);  // Irregular grid with no points
    }
    return Grid(points, order, true);  // Irregular grid
  }

  GridOrder Order() const { return order_; }

  std::vector<int> Size() const {
    if (is_irregular_) {
      return {static_cast<int>(irregular_coordinates_.size()),
              2};  // Each point has two dimensions
    }
    std::vector<int> sizes;
    for (const auto& coord : coordinates_) {
      sizes.push_back(static_cast<int>(coord.size()));
    }
    return sizes;
  }

  Coordinate operator[](size_t index) const {
    if (is_irregular_) {
      return irregular_coordinates_[index];
    }
    size_t dim = coordinates_.size();
    std::vector<int> indices(dim);

    size_t idx = index;
    if (order_ == GridOrder::kC) {
      for (auto&& [coord, ind] : aifocore::ranges::zip(coordinates_, indices)) {
        int size_i = static_cast<int>(coord.size());
        ind = idx % size_i;
        idx /= size_i;
      }
    } else {
      for (int i = static_cast<int>(dim) - 1; i >= 0; --i) {
        int size_i = static_cast<int>(coordinates_[i].size());
        indices[i] = idx % size_i;
        idx /= size_i;
      }
    }

    Coordinate point(dim);
    for (auto&& [coordinate, index, c_point] :
         aifocore::ranges::zip(coordinates_, indices, point)) {
      c_point = coordinate[index];
    }
    return point;
  }

  size_t Length() const {
    if (is_irregular_) {
      return irregular_coordinates_.size();
    }
    return std::accumulate(
        coordinates_.begin(), coordinates_.end(), static_cast<size_t>(1),
        [](size_t acc, const auto& coord) { return acc * coord.size(); });
  }

  GridOrder GetGridOrder() const { return order_; }

  class Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Coordinate;
    using pointer = const Coordinate*;
    using reference = const Coordinate;

    Iterator(const Grid& grid, size_t index) : grid_(grid), index_(index) {}

    value_type operator*() const { return grid_[index_]; }

    Iterator& operator++() {
      ++index_;
      return *this;
    }

    bool operator==(const Iterator& other) const {
      return index_ == other.index_;
    }

    bool operator!=(const Iterator& other) const {
      return index_ != other.index_;
    }

   private:
    const Grid& grid_;
    size_t index_;
  };

  Iterator begin() const { return Iterator(*this, 0); }

  Iterator end() const { return Iterator(*this, Length()); }

 private:
  Coordinates coordinates_;
  std::vector<std::vector<T>> irregular_coordinates_;
  GridOrder order_;
  bool is_irregular_;  // Whether the grid is irregular

  static void ApplyOffset(std::vector<std::vector<T>>& coordinates,
                          const std::vector<T>& offset) {
    if (coordinates.size() != offset.size()) {
      throw std::invalid_argument(
          "Offset must match the dimensions of coordinates.");
    }

    for (auto&& [coord, off] : aifocore::ranges::zip(coordinates, offset)) {
      for (auto& value : coord) {
        value += off;
      }
    }
  }
};

}  // namespace aifocore::tiling

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_TILING_GRID_H_
