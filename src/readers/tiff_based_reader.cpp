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

#include "fastslide/readers/tiff_based_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/concepts/numeric.h"
#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/utilities/tiff/tile_utilities.h"

namespace fs = std::filesystem;

namespace fastslide {

using aifocore::fmt::format;

// Type alias for tile coordinates
using TiffTileCoordinate = tiff::TileCoordinate;

TiffBasedReader::TiffBasedReader(fs::path filename)
    : filename_(std::move(filename)) {}

int TiffBasedReader::GetBestLevelForDownsampleImpl(
    double downsample, int level_count,
    std::function<double(int)> get_level_downsample) const {
  if (level_count == 0) {
    return 0;
  }

  int best_level = 0;
  double best_diff = std::abs(1.0 - downsample);

  for (int level = 0; level < level_count; ++level) {
    double level_downsample = get_level_downsample(level);
    double diff = std::abs(level_downsample - downsample);
    if (diff < best_diff) {
      best_diff = diff;
      best_level = level;
    }
  }

  return best_level;
}

}  // namespace fastslide
