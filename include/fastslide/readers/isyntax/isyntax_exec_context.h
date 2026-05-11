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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_EXEC_CONTEXT_H_

#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include "fastslide/image.h"
#include "fastslide/runtime/cache_interface.h"

namespace isyntax {
class IsyntaxFile;
}  // namespace isyntax

namespace fastslide {

/// @brief Read-only view of iSyntax state needed by the tile executor.
class IsyntaxExecContext {
 public:
  IsyntaxExecContext(std::string_view filename,
                     const isyntax::IsyntaxFile& isyntax_file,
                     std::mutex& mutex, ImageDimensions tile_size,
                     std::shared_ptr<runtime::ITileCache> cache)
      : filename_(filename),
        isyntax_file_(isyntax_file),
        mutex_(mutex),
        tile_size_(tile_size),
        cache_(std::move(cache)) {}

  [[nodiscard]] std::string_view GetFilename() const { return filename_; }

  [[nodiscard]] const isyntax::IsyntaxFile& GetIsyntaxFile() const {
    return isyntax_file_;
  }

  [[nodiscard]] std::mutex& GetMutex() const { return mutex_; }

  [[nodiscard]] ImageDimensions GetTileSize() const { return tile_size_; }

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache_;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache_ != nullptr; }

 private:
  std::string_view filename_;
  const isyntax::IsyntaxFile& isyntax_file_;
  std::mutex& mutex_;
  ImageDimensions tile_size_;
  std::shared_ptr<runtime::ITileCache> cache_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_EXEC_CONTEXT_H_
