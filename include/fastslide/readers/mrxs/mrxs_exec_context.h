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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_EXEC_CONTEXT_H_

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/mrxs/mrxs_data_reader.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide {

/// @brief Read-only view of MRXS reader state needed by the tile executor.
class MrxsExecContext {
 public:
  MrxsExecContext(std::filesystem::path dirname,
                  const mrxs::SlideDataInfo& slide_info,
                  std::shared_ptr<runtime::ITileCache> cache)
      : dirname_(std::move(dirname)),
        slide_info_(slide_info),
        cache_(std::move(cache)) {}

  [[nodiscard]] const mrxs::SlideDataInfo& GetMrxsInfo() const {
    return slide_info_;
  }

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache_;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache_ != nullptr; }

  [[nodiscard]] aifocore::Result<std::vector<uint8_t>> ReadTileData(
      const mrxs::MiraxTileRecord& tile) const {
    return mrxs::MrxsDataReader::ReadTileData(dirname_, tile,
                                              slide_info_.datafile_paths);
  }

 private:
  std::filesystem::path dirname_;
  const mrxs::SlideDataInfo& slide_info_;
  std::shared_ptr<runtime::ITileCache> cache_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_EXEC_CONTEXT_H_
