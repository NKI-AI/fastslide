// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_PATH_UTILS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_PATH_UTILS_H_

#include <filesystem>
#include <system_error>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

/**
 * @file path_utils.h
 * @brief Containment checks for filenames taken from slide files.
 *
 * Several formats are directory bundles whose manifest names the sibling files
 * holding the pixel data: MIRAX reads `FILE_0`.. out of `Slidedat.ini`,
 * OME-Zarr reads `datasets[].path` out of `zarr.json`. Those strings are
 * attacker controlled, so joining them onto the bundle directory without
 * checking lets a crafted slide address any file the process can open.
 */

namespace fastslide {
namespace runtime {
namespace io {

/// @brief Join `relative` onto `root` without allowing an escape from `root`.
///
/// Rejects absolute paths and any `..` component, then resolves the result and
/// confirms it is still inside `root`. Resolution goes through
/// `std::filesystem::weakly_canonical`, so a symlink pointing out of the bundle
/// is rejected as well -- the check would otherwise pass on a path that
/// contains no `..` at all.
///
/// @param root Bundle directory the result must stay within.
/// @param relative Path read out of the slide file.
/// @return Resolved absolute path, or `kInvalidArgument` if it escapes `root`.
[[nodiscard]] inline aifocore::Result<std::filesystem::path>
ResolveContainedPath(const std::filesystem::path& root,
                     const std::filesystem::path& relative) {
  namespace fs = std::filesystem;

  if (relative.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Slide references an empty path");
  }
  if (relative.is_absolute() || relative.has_root_name() ||
      relative.has_root_directory()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Slide references absolute path '{}'",
                              relative.string()));
  }
  for (const auto& component : relative) {
    if (component == "..") {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Slide references parent directory in '{}'",
                                relative.string()));
    }
  }

  std::error_code err;
  const fs::path resolved_root = fs::weakly_canonical(root, err);
  if (err) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Cannot resolve slide directory '{}': {}",
                              root.string(), err.message()));
  }
  const fs::path resolved = fs::weakly_canonical(root / relative, err);
  if (err) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Cannot resolve slide path '{}': {}",
                              relative.string(), err.message()));
  }

  // Compare component-wise rather than as strings so a sibling directory whose
  // name merely starts with the root's name is not mistaken for a child.
  auto root_it = resolved_root.begin();
  auto resolved_it = resolved.begin();
  for (; root_it != resolved_root.end(); ++root_it, ++resolved_it) {
    if (resolved_it == resolved.end() || *resolved_it != *root_it) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "Slide path '{}' resolves to '{}', outside the slide directory "
              "'{}'",
              relative.string(), resolved.string(), resolved_root.string()));
    }
  }

  return resolved;
}

}  // namespace io
}  // namespace runtime

using runtime::io::ResolveContainedPath;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_PATH_UTILS_H_
