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

#include "fastslide/runtime/io/filesystem_utils.h"

#include <cerrno>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {
namespace runtime {
namespace io {

namespace fs = std::filesystem;

namespace {

/// @brief Probes @p path with the non-throwing `status` overload.
///
/// `fs::status` reports a missing path as `file_type::not_found` *and* sets
/// @p ec, so callers must inspect the type before treating @p ec as a failure.
fs::file_status StatusNoThrow(const fs::path& path,
                              std::error_code& ec) noexcept {
  ec.clear();
  return fs::status(path, ec);
}

/// @brief True when a stale network handle caused @p ec.
///
/// `ESTALE` has no `std::errc` equivalent but is the common failure on an
/// expired NFS mount, which is where FastSlide reads most slides from.
bool IsStaleHandle(const std::error_code& ec) noexcept {
#ifdef ESTALE
  return ec.category() == std::system_category() && ec.value() == ESTALE;
#else
  return false;
#endif
}

}  // namespace

aifocore::StatusCode StatusCodeForFilesystemError(const std::error_code& ec) {
  if (ec == std::errc::permission_denied ||
      ec == std::errc::operation_not_permitted) {
    return aifocore::StatusCode::kPermissionDenied;
  }
  if (ec == std::errc::no_such_file_or_directory ||
      ec == std::errc::not_a_directory) {
    return aifocore::StatusCode::kNotFound;
  }
  if (ec == std::errc::io_error || ec == std::errc::no_such_device ||
      ec == std::errc::device_or_resource_busy || ec == std::errc::timed_out ||
      ec == std::errc::host_unreachable || ec == std::errc::network_down ||
      ec == std::errc::network_unreachable ||
      ec == std::errc::too_many_files_open ||
      ec == std::errc::too_many_files_open_in_system || IsStaleHandle(ec)) {
    return aifocore::StatusCode::kUnavailable;
  }
  if (ec == std::errc::filename_too_long ||
      ec == std::errc::too_many_symbolic_link_levels) {
    return aifocore::StatusCode::kInvalidArgument;
  }
  return aifocore::StatusCode::kInternal;
}

aifocore::Status RequireExists(const fs::path& path,
                               std::string_view description) {
  std::error_code ec;
  const fs::file_status status = StatusNoThrow(path, ec);

  if (status.type() == fs::file_type::not_found) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("{} does not exist: {}", description,
                              path.string()));
  }
  if (ec) {
    return AIFOCORE_MAKE_STATUS(
        StatusCodeForFilesystemError(ec),
        aifocore::fmt::format("Cannot access {} at {}: {}", description,
                              path.string(), ec.message()));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status RequireDirectory(const fs::path& path,
                                  std::string_view description) {
  std::error_code ec;
  const fs::file_status status = StatusNoThrow(path, ec);

  if (status.type() == fs::file_type::not_found) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("{} does not exist: {}", description,
                              path.string()));
  }
  if (ec) {
    return AIFOCORE_MAKE_STATUS(
        StatusCodeForFilesystemError(ec),
        aifocore::fmt::format("Cannot access {} at {}: {}", description,
                              path.string(), ec.message()));
  }
  if (!fs::is_directory(status)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("{} is not a directory: {}", description,
                              path.string()));
  }
  return aifocore::Status::OkStatus();
}

bool IsDirectoryOrFalse(const fs::path& path) noexcept {
  std::error_code ec;
  return fs::is_directory(path, ec) && !ec;
}

aifocore::Result<std::vector<fs::path>> ListDirectory(const fs::path& dir) {
  std::error_code ec;
  fs::directory_iterator it(dir, ec);
  if (ec) {
    return AIFOCORE_MAKE_STATUS(
        StatusCodeForFilesystemError(ec),
        aifocore::fmt::format("Cannot list directory {}: {}", dir.string(),
                              ec.message()));
  }

  std::vector<fs::path> entries;
  const fs::directory_iterator end;
  while (it != end) {
    entries.push_back(it->path());
    // The incrementing overload taking an error_code is the only non-throwing
    // way to advance: a directory can become unreadable mid-scan.
    it.increment(ec);
    if (ec) {
      return AIFOCORE_MAKE_STATUS(
          StatusCodeForFilesystemError(ec),
          aifocore::fmt::format("Failed while listing directory {}: {}",
                                dir.string(), ec.message()));
    }
  }
  return entries;
}

}  // namespace io
}  // namespace runtime
}  // namespace fastslide
