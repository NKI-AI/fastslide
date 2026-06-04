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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_EXEC_CONTEXT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "fastslide/runtime/cache_interface.h"
#include "simpletiff/index.h"

namespace fastslide {

/// @brief Read-only view of NDPI reader state needed by the tile executor.
///
/// Mirrors the AperioExecContext shape (filename + cache + tiff_index) so the
/// CRTP `CachedTileExecutor<NdpiTiffTileExecutor>` can call `IsCacheEnabled()`
/// and `GetCache()` directly on the context. The NDPI-specific JPEG header
/// template fields are needed by the headerless-tile decode path.
class NdpiTiffExecContext {
 public:
  NdpiTiffExecContext(std::string filename,
                      std::shared_ptr<runtime::ITileCache> cache,
                      const simpletiff::TiffIndex& tiff_index, int level_count,
                      std::span<const uint8_t> jpeg_header_template,
                      std::span<const size_t> sof_height_offsets,
                      std::span<const size_t> sof_width_offsets)
      : filename_(std::move(filename)),
        cache_(std::move(cache)),
        tiff_index_(tiff_index),
        level_count_(level_count),
        jpeg_header_template_(jpeg_header_template),
        sof_height_offsets_(sof_height_offsets),
        sof_width_offsets_(sof_width_offsets) {}

  [[nodiscard]] const std::string& GetFilename() const { return filename_; }

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache_;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache_ != nullptr; }

  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return tiff_index_;
  }

  [[nodiscard]] int GetLevelCount() const { return level_count_; }

  [[nodiscard]] std::span<const uint8_t> GetJpegHeaderTemplate() const {
    return jpeg_header_template_;
  }

  [[nodiscard]] std::span<const size_t> GetSofHeightOffsets() const {
    return sof_height_offsets_;
  }

  [[nodiscard]] std::span<const size_t> GetSofWidthOffsets() const {
    return sof_width_offsets_;
  }

 private:
  std::string filename_;
  std::shared_ptr<runtime::ITileCache> cache_;
  const simpletiff::TiffIndex& tiff_index_;
  int level_count_;
  std::span<const uint8_t> jpeg_header_template_;
  std::span<const size_t> sof_height_offsets_;
  std::span<const size_t> sof_width_offsets_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_EXEC_CONTEXT_H_
