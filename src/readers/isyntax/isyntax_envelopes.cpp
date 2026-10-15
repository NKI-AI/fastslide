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

#include "fastslide/readers/isyntax/isyntax_envelopes.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace fastslide {

namespace {

using Polygon = IsyntaxEnvelopes::Polygon;
using Point = IsyntaxEnvelopes::Point;

/// Edge of an orthogonal polygon: `at` is its x (vertical) or y (horizontal),
/// it spans `from` to `to` on the other axis.
struct Edge {
  int64_t at;
  int64_t from;
  int64_t to;
};

int64_t FloorDiv2(int64_t v) {
  return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

/// First pixel of a range after `levels` wavelet levels.
int64_t ErodeStart(int64_t v, int32_t levels) {
  for (int32_t l = 0; l < levels; ++l) {
    v = -FloorDiv2(-v) + 1;  // ceil(v / 2) + 1
  }
  return v;
}

/// Last pixel (included) of a range after `levels` wavelet levels.
int64_t ErodeEnd(int64_t v, int32_t levels) {
  for (int32_t l = 0; l < levels; ++l) {
    v = FloorDiv2(v) - 1;
  }
  return v;
}

/// Pairs the vertices of each column (vertical edges) or row (horizontal
/// edges): an orthogonal polygon is defined by its vertex set alone.
std::vector<Edge> Edges(const Polygon& polygon, bool vertical) {
  std::map<int64_t, std::vector<int64_t>> lines;
  for (const Point& p : polygon) {
    lines[vertical ? p.x : p.y].push_back(vertical ? p.y : p.x);
  }
  std::vector<Edge> edges;
  for (auto& [at, values] : lines) {
    std::sort(values.begin(), values.end());
    for (size_t i = 0; i + 1 < values.size(); i += 2) {
      edges.push_back({at, values[i], values[i + 1]});
    }
  }
  return edges;
}

/// Whether the centre of pixel (x, y) is in the polygon: count the vertical
/// edges on its left.
bool Contains(const std::vector<Edge>& vertical, int64_t x, int64_t y) {
  int crossings = 0;
  for (const Edge& e : vertical) {
    if (e.at <= x && e.from <= y && y < e.to) {
      ++crossings;
    }
  }
  return crossings % 2 == 1;
}

/// The polygon at `level`: every edge moved as the SDK erodes it.
Polygon Erode(const Polygon& polygon, int32_t level) {
  const auto vertical = Edges(polygon, /*vertical=*/true);
  const auto horizontal = Edges(polygon, /*vertical=*/false);
  std::map<std::pair<int64_t, int64_t>, Point> moved;
  for (const Edge& e : vertical) {
    // The polygon starts at this edge when it holds the pixel on its right.
    const int64_t x = Contains(vertical, e.at, e.from)
                          ? ErodeStart(e.at, level)
                          : ErodeEnd(e.at - 1, level) + 1;
    moved[{e.at, e.from}].x = x;
    moved[{e.at, e.to}].x = x;
  }
  for (const Edge& e : horizontal) {
    // The polygon starts at this edge when it holds the pixel below it.
    const int64_t y = Contains(vertical, e.from, e.at)
                          ? ErodeStart(e.at, level)
                          : ErodeEnd(e.at - 1, level) + 1;
    moved[{e.from, e.at}].y = y;
    moved[{e.to, e.at}].y = y;
  }
  Polygon eroded;
  eroded.reserve(polygon.size());
  for (const Point& p : polygon) {
    eroded.push_back(moved[{p.x, p.y}]);
  }
  return eroded;
}

/// Vertical strips of the polygon, merged when two neighbours span the same
/// rows.
std::vector<SlideBounds> Strips(const Polygon& polygon) {
  const auto vertical = Edges(polygon, /*vertical=*/true);
  std::vector<int64_t> xs;
  std::vector<int64_t> ys;
  for (const Edge& e : vertical) {
    xs.push_back(e.at);
    ys.push_back(e.from);
    ys.push_back(e.to);
  }
  std::sort(xs.begin(), xs.end());
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  std::sort(ys.begin(), ys.end());
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

  std::vector<SlideBounds> strips;
  for (size_t i = 0; i + 1 < xs.size(); ++i) {
    const int64_t x0 = xs[i];
    const int64_t x1 = xs[i + 1];
    int64_t run_start = -1;
    int64_t run_end = -1;
    const auto flush = [&] {
      if (run_start < 0) {
        return;
      }
      SlideBounds strip(x0, run_start, x1 - x0, run_end - run_start);
      // Extend the previous strip when it spans the same rows right before.
      auto previous =
          std::find_if(strips.rbegin(), strips.rend(), [&](const auto& s) {
            return s.x + s.width == x0 && s.y == strip.y &&
                   s.height == strip.height;
          });
      if (previous != strips.rend()) {
        previous->width += strip.width;
      } else {
        strips.push_back(strip);
      }
      run_start = -1;
    };
    for (size_t j = 0; j + 1 < ys.size(); ++j) {
      if (Contains(vertical, x0, ys[j])) {
        if (run_start < 0 || run_end != ys[j]) {
          flush();
          run_start = ys[j];
        }
        run_end = ys[j + 1];
      }
    }
    flush();
  }
  return strips;
}

}  // namespace

IsyntaxEnvelopes IsyntaxEnvelopes::FromHeader(
    std::vector<Polygon> grid_polygons, int32_t level0_padding) {
  for (Polygon& polygon : grid_polygons) {
    for (Point& p : polygon) {
      p.x -= level0_padding;
      p.y -= level0_padding;
    }
  }
  std::erase_if(grid_polygons, [](const Polygon& p) { return p.size() < 4; });
  return IsyntaxEnvelopes(std::move(grid_polygons));
}

IsyntaxEnvelopes IsyntaxEnvelopes::FromTiles(std::span<const uint8_t> present,
                                             int64_t width_in_tiles,
                                             int64_t tile_width,
                                             int64_t tile_height,
                                             int32_t level0_padding,
                                             int64_t width, int64_t height) {
  std::vector<Polygon> boxes;
  if (width_in_tiles <= 0) {
    return IsyntaxEnvelopes(std::move(boxes));
  }
  const auto height_in_tiles =
      static_cast<int64_t>(present.size()) / width_in_tiles;
  std::vector<bool> seen(present.size(), false);
  std::deque<int64_t> queue;
  for (int64_t start = 0; start < static_cast<int64_t>(present.size());
       ++start) {
    if (!present[start] || seen[start]) {
      continue;
    }
    // Box around the group of tiles connected to this one.
    int64_t tx0 = start % width_in_tiles;
    int64_t ty0 = start / width_in_tiles;
    int64_t tx1 = tx0;
    int64_t ty1 = ty0;
    seen[start] = true;
    queue.push_back(start);
    while (!queue.empty()) {
      const int64_t index = queue.front();
      queue.pop_front();
      const int64_t tx = index % width_in_tiles;
      const int64_t ty = index / width_in_tiles;
      tx0 = std::min(tx0, tx);
      tx1 = std::max(tx1, tx);
      ty0 = std::min(ty0, ty);
      ty1 = std::max(ty1, ty);
      const std::pair<int64_t, int64_t> neighbours[] = {
          {tx - 1, ty}, {tx + 1, ty}, {tx, ty - 1}, {tx, ty + 1}};
      for (const auto& [nx, ny] : neighbours) {
        if (nx < 0 || ny < 0 || nx >= width_in_tiles || ny >= height_in_tiles) {
          continue;
        }
        const int64_t next = ny * width_in_tiles + nx;
        if (present[next] && !seen[next]) {
          seen[next] = true;
          queue.push_back(next);
        }
      }
    }
    const int64_t x0 = std::max<int64_t>(tx0 * tile_width - level0_padding, 0);
    const int64_t y0 = std::max<int64_t>(ty0 * tile_height - level0_padding, 0);
    const int64_t x1 =
        std::min<int64_t>((tx1 + 1) * tile_width - level0_padding, width);
    const int64_t y1 =
        std::min<int64_t>((ty1 + 1) * tile_height - level0_padding, height);
    if (x1 > x0 && y1 > y0) {
      boxes.push_back({{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}});
    }
  }
  return IsyntaxEnvelopes(std::move(boxes));
}

SlideBounds IsyntaxEnvelopes::Bounds(int64_t width, int64_t height) const {
  int64_t x0 = std::numeric_limits<int64_t>::max();
  int64_t y0 = std::numeric_limits<int64_t>::max();
  int64_t x1 = std::numeric_limits<int64_t>::min();
  int64_t y1 = std::numeric_limits<int64_t>::min();
  for (const Polygon& polygon : polygons_) {
    for (const Point& p : polygon) {
      x0 = std::min(x0, p.x);
      y0 = std::min(y0, p.y);
      x1 = std::max(x1, p.x);
      y1 = std::max(y1, p.y);
    }
  }
  x0 = std::max<int64_t>(x0, 0);
  y0 = std::max<int64_t>(y0, 0);
  x1 = std::min(x1, width);
  y1 = std::min(y1, height);
  if (x1 <= x0 || y1 <= y0) {
    return {};
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

std::vector<SlideBounds> IsyntaxEnvelopes::Regions(int32_t level) const {
  std::vector<SlideBounds> regions;
  for (const Polygon& polygon : polygons_) {
    for (const SlideBounds& strip : Strips(Erode(polygon, level))) {
      if (strip.IsValid()) {
        regions.push_back(strip);
      }
    }
  }
  return regions;
}

}  // namespace fastslide
