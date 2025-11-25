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

#include "fastslide/utilities/tiff/tile_utilities.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace fastslide {
namespace tiff {

TileCoordinateIterator::TileCoordinateIterator(uint32_t start_x,
                                               uint32_t start_y, uint32_t end_x,
                                               uint32_t end_y,
                                               uint32_t tile_width,
                                               uint32_t tile_height)
    : current_x_(start_x),
      current_y_(start_y),
      start_x_(start_x),
      end_x_(end_x),
      end_y_(end_y),
      tile_width_(tile_width),
      tile_height_(tile_height),
      current_coord_{start_x, start_y},
      is_end_(start_y >= end_y) {}

TileCoordinateIterator::TileCoordinateIterator()
    : current_x_(0),
      current_y_(0),
      start_x_(0),
      end_x_(0),
      end_y_(0),
      tile_width_(0),
      tile_height_(0),
      current_coord_{0, 0},
      is_end_(true) {}

TileCoordinateIterator::reference TileCoordinateIterator::operator*() const {
  return current_coord_;
}

TileCoordinateIterator::pointer TileCoordinateIterator::operator->() const {
  return &current_coord_;
}

TileCoordinateIterator& TileCoordinateIterator::operator++() {
  if (is_end_) {
    return *this;
  }

  current_x_ += tile_width_;
  if (current_x_ >= end_x_) {
    current_x_ = start_x_;
    current_y_ += tile_height_;
    if (current_y_ >= end_y_) {
      is_end_ = true;
      return *this;
    }
  }
  current_coord_ = {current_x_, current_y_};
  return *this;
}

TileCoordinateIterator TileCoordinateIterator::operator++(int) {
  TileCoordinateIterator tmp = *this;
  ++(*this);
  return tmp;
}

bool operator==(const TileCoordinateIterator& a,
                const TileCoordinateIterator& b) {
  if (a.is_end_ && b.is_end_) {
    return true;
  }
  if (a.is_end_ || b.is_end_) {
    return false;
  }
  return a.current_x_ == b.current_x_ && a.current_y_ == b.current_y_;
}

bool operator!=(const TileCoordinateIterator& a,
                const TileCoordinateIterator& b) {
  return !(a == b);
}

TileCoordinateRange::TileCoordinateRange(
    const RegionSpec& region, const aifocore::Size<uint32_t, 2>& tile_dims)
    : start_x_((region.top_left[0] / tile_dims[0]) * tile_dims[0]),
      start_y_((region.top_left[1] / tile_dims[1]) * tile_dims[1]),
      end_x_(region.top_left[0] + region.size[0]),
      end_y_(region.top_left[1] + region.size[1]),
      tile_width_(tile_dims[0]),
      tile_height_(tile_dims[1]) {}

TileCoordinateIterator TileCoordinateRange::begin() const {
  return TileCoordinateIterator(start_x_, start_y_, end_x_, end_y_, tile_width_,
                                tile_height_);
}

TileCoordinateIterator TileCoordinateRange::end() const {
  return TileCoordinateIterator();
}

void CopyTileToBuffer(const uint8_t* tile_buffer, uint8_t* output_buffer,
                      uint32_t tile_width, uint32_t tile_height,
                      uint32_t tile_x, uint32_t tile_y, uint32_t region_x,
                      uint32_t region_y, uint32_t region_width,
                      uint32_t region_height, uint32_t bytes_per_pixel) {
  const uint32_t x0 = std::max(region_x, tile_x);
  const uint32_t y0 = std::max(region_y, tile_y);
  const uint32_t x1 = std::min(region_x + region_width, tile_x + tile_width);
  const uint32_t y1 = std::min(region_y + region_height, tile_y + tile_height);

  if (x0 >= x1 || y0 >= y1) {
    return;  // No intersection
  }

  const uint32_t rows = y1 - y0;
  const uint32_t cols = x1 - x0;

  const size_t copy_bytes = size_t(cols) * bytes_per_pixel;
  const size_t tile_row_stride = size_t(tile_width) * bytes_per_pixel;
  const size_t out_row_stride = size_t(region_width) * bytes_per_pixel;

  const size_t tile_xoff = size_t(x0 - tile_x) * bytes_per_pixel;
  const size_t out_xoff = size_t(x0 - region_x) * bytes_per_pixel;

  const uint8_t* src =
      tile_buffer + size_t(y0 - tile_y) * tile_row_stride + tile_xoff;
  uint8_t* dst =
      output_buffer + size_t(y0 - region_y) * out_row_stride + out_xoff;

  for (uint32_t r = 0; r < rows; ++r) {
    std::memcpy(dst, src, copy_bytes);
    src += tile_row_stride;
    dst += out_row_stride;
  }
}

}  // namespace tiff
}  // namespace fastslide
