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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_EXEC_CONTEXT_H_

#include <memory>
#include <string>

#include "fastslide/jpeg_decode_mode.h"
#include "fastslide/runtime/cache_interface.h"
#include "simpletiff/index.h"

namespace fastslide {

/// @brief Read-only view into the Aperio reader state needed by the tile
/// executor.
///
/// The executor used to take `const AperioReader&` and call
/// `reader.GetFilename()`, `reader.GetTiffIndex()`, `reader.IsCacheEnabled()`,
/// and `reader.GetCache()`. Those callbacks created a call-graph cycle
/// between `aperio.cpp` and `aperio_tile_executor.cpp`. The reader now
/// builds an `AperioExecContext` once per `ExecutePlan` invocation and hands
/// it to the executor.
///
/// The accessor methods (rather than plain fields) are kept so the context is
/// drop-in compatible with the existing `CachedTileExecutor<...>` CRTP
/// templates, which call `IsCacheEnabled()`/`GetCache()` on whatever object
/// is passed to them.
class AperioExecContext {
 public:
  AperioExecContext(std::string filename,
                    std::shared_ptr<runtime::ITileCache> cache,
                    const simpletiff::TiffIndex& tiff_index,
                    JpegDecodeMode jpeg_decode_mode)
      : filename_(std::move(filename)),
        cache_(std::move(cache)),
        tiff_index_(tiff_index),
        jpeg_decode_mode_(jpeg_decode_mode) {}

  [[nodiscard]] const std::string& GetFilename() const { return filename_; }

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache_;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache_ != nullptr; }

  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return tiff_index_;
  }

  /// JPEG decode mode of the owning reader, fixed for this plan.
  [[nodiscard]] JpegDecodeMode GetJpegDecodeMode() const {
    return jpeg_decode_mode_;
  }

 private:
  std::string filename_;
  std::shared_ptr<runtime::ITileCache> cache_;
  const simpletiff::TiffIndex& tiff_index_;
  JpegDecodeMode jpeg_decode_mode_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_EXEC_CONTEXT_H_
