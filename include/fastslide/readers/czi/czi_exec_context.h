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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_EXEC_CONTEXT_H_

#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "fastslide/jpeg_decode_mode.h"
#include "fastslide/readers/czi/czi_level_info.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide {

/// @brief Read-only view of CZI state needed by the tile executor.
class CziExecContext {
 public:
  CziExecContext(std::string_view filename,
                 std::span<const CziSubblockInfo> subblocks,
                 std::shared_ptr<runtime::ITileCache> cache, bool is_spectral,
                 JpegDecodeMode jpeg_decode_mode)
      : filename_(filename),
        subblocks_(subblocks),
        cache_(std::move(cache)),
        is_spectral_(is_spectral),
        jpeg_decode_mode_(jpeg_decode_mode) {}

  [[nodiscard]] std::string_view GetFilename() const { return filename_; }

  [[nodiscard]] const CziSubblockInfo& GetSubblockInfo(uint32_t index) const {
    return subblocks_[index];
  }

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache_;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache_ != nullptr; }

  /// @brief True when decoding a multi-channel (spectral) plan: each subblock
  ///        yields a single-channel plane rather than an RGB triplet.
  [[nodiscard]] bool IsSpectral() const { return is_spectral_; }

  /// JPEG decode mode of the owning reader, fixed for this plan.
  [[nodiscard]] JpegDecodeMode GetJpegDecodeMode() const {
    return jpeg_decode_mode_;
  }

 private:
  std::string_view filename_;
  std::span<const CziSubblockInfo> subblocks_;
  std::shared_ptr<runtime::ITileCache> cache_;
  bool is_spectral_ = false;
  JpegDecodeMode jpeg_decode_mode_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_EXEC_CONTEXT_H_
