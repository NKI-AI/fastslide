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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_EXEC_CONTEXT_H_

#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "fastslide/readers/ometiff/ometiff_level_info.h"
#include "fastslide/runtime/cache_interface.h"
#include "simpletiff/index.h"

namespace fastslide {

/// @brief Read-only view of OME-TIFF state needed by the tile executor.
class OmetiffExecContext {
 public:
  OmetiffExecContext(std::string_view filename,
                     std::span<const OmeTiffLevelInfo> pyramid,
                     const simpletiff::TiffIndex& tiff_index,
                     std::shared_ptr<runtime::ITileCache> cache)
      : filename_(filename),
        pyramid_(pyramid),
        tiff_index_(tiff_index),
        cache_(std::move(cache)) {}

  [[nodiscard]] std::string_view GetFilename() const { return filename_; }

  [[nodiscard]] std::span<const OmeTiffLevelInfo> GetPyramid() const {
    return pyramid_;
  }

  [[nodiscard]] const simpletiff::TiffIndex& GetTiffIndex() const {
    return tiff_index_;
  }

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache_;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache_ != nullptr; }

 private:
  std::string_view filename_;
  std::span<const OmeTiffLevelInfo> pyramid_;
  const simpletiff::TiffIndex& tiff_index_;
  std::shared_ptr<runtime::ITileCache> cache_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMETIFF_OMETIFF_EXEC_CONTEXT_H_
