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

/// @file filesystem_utils_test.cpp
/// @brief Tests that filesystem probes report status instead of throwing.

#include "fastslide/runtime/io/filesystem_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include "aifocore/status/result.h"

namespace fastslide::runtime::io {
namespace {

namespace fs = std::filesystem;

/// @brief True when permission bits cannot be tested because root bypasses
/// them.
bool RunningAsRoot() {
#if defined(__unix__) || defined(__APPLE__)
  return ::geteuid() == 0;
#else
  return false;
#endif
}

/// @brief Unique temporary directory that restores permissions before removal.
///
/// A test that drops all permissions on a subdirectory would otherwise leave a
/// tree that `remove_all` cannot delete.
class TempDir {
 public:
  TempDir() {
    std::error_code ec;
    const auto base = fs::temp_directory_path(ec);
    path_ = base /
            ("fastslide_fs_utils_test_" +
             std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
             "_" + std::to_string(counter_++));
    fs::remove_all(path_, ec);
    fs::create_directories(path_, ec);
  }

  ~TempDir() {
    std::error_code ec;
    // Restore traversal rights on every entry so the tree can be removed.
    for (fs::recursive_directory_iterator it(
             path_, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      std::error_code perm_ec;
      fs::permissions(it->path(), fs::perms::owner_all, perm_ec);
    }
    fs::permissions(path_, fs::perms::owner_all, ec);
    fs::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const fs::path& path() const { return path_; }

  /// @brief Creates a file containing @p contents under the temp directory.
  fs::path WriteFile(const fs::path& relative,
                     const std::string& contents = "x") const {
    const fs::path target = path_ / relative;
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    std::ofstream out(target);
    out << contents;
    return target;
  }

  /// @brief Creates a subdirectory under the temp directory.
  fs::path MakeDir(const fs::path& relative) const {
    const fs::path target = path_ / relative;
    std::error_code ec;
    fs::create_directories(target, ec);
    return target;
  }

  /// @brief Removes all permission bits from @p path.
  static void DenyAll(const fs::path& path) {
    std::error_code ec;
    fs::permissions(path, fs::perms::none, ec);
    ASSERT_FALSE(ec) << "could not drop permissions: " << ec.message();
  }

 private:
  static int counter_;
  fs::path path_;
};

int TempDir::counter_ = 0;

TEST(RequireExists, ReturnsOkForExistingFile) {
  const TempDir dir;
  const fs::path file = dir.WriteFile("slide.mrxs");

  EXPECT_TRUE(RequireExists(file, "slide file").ok());
}

TEST(RequireExists, ReturnsNotFoundForMissingFile) {
  const TempDir dir;

  const auto status = RequireExists(dir.path() / "absent.mrxs", "slide file");

  EXPECT_EQ(status.code(), aifocore::StatusCode::kNotFound);
}

// Regression test for the crash reported from slideinsight: a slide directory
// the process may not traverse made `fs::exists` throw `filesystem_error`,
// which unwound out of the C ABI and aborted the host process.
TEST(RequireExists, ReportsPermissionDeniedInsteadOfThrowing) {
  if (RunningAsRoot()) {
    GTEST_SKIP() << "root bypasses permission bits";
  }
  const TempDir dir;
  const fs::path slide_dir = dir.MakeDir("4237T1");
  dir.WriteFile("4237T1/Slidedat.ini");
  ASSERT_NO_FATAL_FAILURE(TempDir::DenyAll(slide_dir));

  aifocore::Status status;
  ASSERT_NO_THROW(
      status = RequireExists(slide_dir / "Slidedat.ini", "Slidedat.ini"));

  EXPECT_EQ(status.code(), aifocore::StatusCode::kPermissionDenied);
  EXPECT_NE(std::string(status.message()).find("Slidedat.ini"),
            std::string::npos);
}

TEST(RequireDirectory, DistinguishesFileFromDirectory) {
  const TempDir dir;
  const fs::path file = dir.WriteFile("zarr.json");
  const fs::path subdir = dir.MakeDir("root.zarr");

  EXPECT_TRUE(RequireDirectory(subdir, "OME-Zarr root").ok());
  EXPECT_EQ(RequireDirectory(file, "OME-Zarr root").code(),
            aifocore::StatusCode::kInvalidArgument);
  EXPECT_EQ(RequireDirectory(dir.path() / "absent", "OME-Zarr root").code(),
            aifocore::StatusCode::kNotFound);
}

TEST(RequireDirectory, ReportsPermissionDeniedInsteadOfThrowing) {
  if (RunningAsRoot()) {
    GTEST_SKIP() << "root bypasses permission bits";
  }
  const TempDir dir;
  const fs::path outer = dir.MakeDir("outer");
  const fs::path inner = dir.MakeDir("outer/inner");
  ASSERT_NO_FATAL_FAILURE(TempDir::DenyAll(outer));

  aifocore::Status status;
  ASSERT_NO_THROW(status = RequireDirectory(inner, "data directory"));

  EXPECT_EQ(status.code(), aifocore::StatusCode::kPermissionDenied);
}

TEST(IsDirectoryOrFalse, ReportsFalseForNonDirectoriesAndErrors) {
  const TempDir dir;
  const fs::path file = dir.WriteFile("frame_t.ets");
  const fs::path subdir = dir.MakeDir("stack1");

  EXPECT_TRUE(IsDirectoryOrFalse(subdir));
  EXPECT_FALSE(IsDirectoryOrFalse(file));
  EXPECT_FALSE(IsDirectoryOrFalse(dir.path() / "absent"));
}

TEST(IsDirectoryOrFalse, DoesNotThrowOnUnreadableParent) {
  if (RunningAsRoot()) {
    GTEST_SKIP() << "root bypasses permission bits";
  }
  const TempDir dir;
  const fs::path outer = dir.MakeDir("outer");
  const fs::path inner = dir.MakeDir("outer/inner");
  ASSERT_NO_FATAL_FAILURE(TempDir::DenyAll(outer));

  bool result = true;
  ASSERT_NO_THROW(result = IsDirectoryOrFalse(inner));
  EXPECT_FALSE(result);
}

TEST(ListDirectory, ReturnsEveryEntry) {
  const TempDir dir;
  dir.WriteFile("series/1.dcm");
  dir.WriteFile("series/2.dcm");
  dir.MakeDir("series/nested");

  auto entries_or = ListDirectory(dir.path() / "series");
  ASSERT_TRUE(entries_or.ok()) << entries_or.status().message();

  std::vector<std::string> names;
  for (const fs::path& entry : entries_or.value()) {
    names.push_back(entry.filename().string());
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(names, (std::vector<std::string>{"1.dcm", "2.dcm", "nested"}));
}

TEST(ListDirectory, ReportsPermissionDeniedInsteadOfThrowing) {
  if (RunningAsRoot()) {
    GTEST_SKIP() << "root bypasses permission bits";
  }
  const TempDir dir;
  const fs::path series = dir.MakeDir("series");
  dir.WriteFile("series/1.dcm");
  ASSERT_NO_FATAL_FAILURE(TempDir::DenyAll(series));

  EXPECT_NO_THROW({
    auto entries_or = ListDirectory(series);
    ASSERT_FALSE(entries_or.ok());
    EXPECT_EQ(entries_or.status().code(),
              aifocore::StatusCode::kPermissionDenied);
  });
}

TEST(ListDirectory, ReportsNotFoundForMissingDirectory) {
  const TempDir dir;

  auto entries_or = ListDirectory(dir.path() / "absent");

  ASSERT_FALSE(entries_or.ok());
  EXPECT_EQ(entries_or.status().code(), aifocore::StatusCode::kNotFound);
}

TEST(StatusCodeForFilesystemError, MapsErrnoOntoStatusCodes) {
  const auto code_for = [](std::errc value) {
    return StatusCodeForFilesystemError(std::make_error_code(value));
  };

  EXPECT_EQ(code_for(std::errc::permission_denied),
            aifocore::StatusCode::kPermissionDenied);
  EXPECT_EQ(code_for(std::errc::no_such_file_or_directory),
            aifocore::StatusCode::kNotFound);
  EXPECT_EQ(code_for(std::errc::io_error), aifocore::StatusCode::kUnavailable);
  EXPECT_EQ(code_for(std::errc::filename_too_long),
            aifocore::StatusCode::kInvalidArgument);
  EXPECT_EQ(code_for(std::errc::not_enough_memory),
            aifocore::StatusCode::kInternal);
}

}  // namespace
}  // namespace fastslide::runtime::io
