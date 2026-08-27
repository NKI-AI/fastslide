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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILESYSTEM_UTILS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILESYSTEM_UTILS_H_

/// @file filesystem_utils.h
/// @brief Non-throwing `std::filesystem` probes that report `aifocore::Status`.
///
/// The path-taking `std::filesystem` overloads throw `filesystem_error` on any
/// error that is not "the path does not exist" — an unreadable parent
/// directory, a stale network mount, or a dangling symlink all raise. FastSlide
/// reports failures with `aifocore::Status`, and its C ABI cannot let an
/// exception unwind into a cgo, JNI or ctypes caller, so readers must use the
/// `std::error_code` overloads instead. These helpers wrap that pattern and, in
/// doing so, keep the distinction the throwing overloads collapse: a file that
/// is absent yields `kNotFound`, whereas a file that cannot be *queried* yields
/// `kPermissionDenied` or `kUnavailable` with the OS message attached.
///
/// @code
/// AIFOCORE_RETURN_IF_ERROR(
///     runtime::io::RequireExists(slidedat_path, "Slidedat.ini"));
/// @endcode

#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

#include "aifocore/status/result.h"

namespace fastslide {
namespace runtime {
namespace io {

/// @brief Maps a filesystem `std::error_code` onto the closest status code.
///
/// @param ec Error code reported by a `std::filesystem` call; must be set
/// @return `kNotFound` for a missing path, `kPermissionDenied` for an
///         inaccessible one, `kUnavailable` for a transient I/O or mount
///         failure, and `kInternal` for anything else
aifocore::StatusCode StatusCodeForFilesystemError(const std::error_code& ec);

/// @brief Returns OK only when @p path exists.
///
/// Replaces `if (!fs::exists(path))`, which throws when the path's status
/// cannot be determined rather than returning false.
///
/// @param path Path to probe
/// @param description Human-readable name of @p path, used in the message
/// @return OK if @p path exists, `kNotFound` if it does not, or the mapped
///         status when its existence cannot be determined
aifocore::Status RequireExists(const std::filesystem::path& path,
                               std::string_view description);

/// @brief Returns OK only when @p path exists and is a directory.
///
/// @param path Path to probe
/// @param description Human-readable name of @p path, used in the message
/// @return OK if @p path is a directory, `kNotFound` if it does not exist,
///         `kInvalidArgument` if it exists but is not a directory, or the
///         mapped status when its status cannot be determined
aifocore::Status RequireDirectory(const std::filesystem::path& path,
                                  std::string_view description);

/// @brief Non-throwing `fs::is_directory` for use as a branch condition.
///
/// Reports false both for "not a directory" and for "status unknown". Use only
/// where an unreadable path should take the same branch as a non-directory; if
/// the caller must surface the reason, use `RequireDirectory` instead.
///
/// @param path Path to probe
/// @return True only if @p path is known to be a directory
bool IsDirectoryOrFalse(const std::filesystem::path& path) noexcept;

/// @brief Non-throwing directory listing.
///
/// Replaces `fs::directory_iterator(dir)`, which throws both on construction
/// and while advancing. Entries are returned in directory order.
///
/// @param dir Directory to list
/// @return The entries in @p dir, or the mapped status if it cannot be read
aifocore::Result<std::vector<std::filesystem::path>> ListDirectory(
    const std::filesystem::path& dir);

}  // namespace io
}  // namespace runtime
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILESYSTEM_UTILS_H_
