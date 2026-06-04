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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TIFF_METADATA_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TIFF_METADATA_H_

#include <cstdint>

namespace fastslide {

/// @brief TIFF structure metadata needed for Aperio tile execution.
struct TiffStructureMetadata {
  uint16_t page = 0;               ///< TIFF page/directory number
  uint32_t tile_width = 0;         ///< Tile width in pixels
  uint32_t tile_height = 0;        ///< Tile height (or rows per strip)
  uint16_t samples_per_pixel = 3;  ///< Number of channels (typically 3 for RGB)
  bool is_tiled = true;            ///< Whether TIFF uses tiles (vs strips)
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_TIFF_METADATA_H_
