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

/// @file mrxs_open_errors_test.cpp
/// @brief Opening an unreadable MRXS slide must report status, never throw.
///
/// An MRXS slide is a `.mrxs` stub plus a sibling directory holding
/// `Slidedat.ini`. When that directory is not traversable, probing for
/// `Slidedat.ini` used to raise `std::filesystem::filesystem_error`; because
/// FastSlide is loaded through a C ABI the exception had no handler in the
/// caller's frame and aborted the host process.

#include "fastslide/readers/mrxs/mrxs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include "aifocore/status/result.h"

namespace fastslide {
namespace {

namespace fs = std::filesystem;

bool RunningAsRoot() {
#if defined(__unix__) || defined(__APPLE__)
  return ::geteuid() == 0;
#else
  return false;
#endif
}

/// @brief Lays out a minimal MRXS slide in a temporary directory.
class MrxsSlideFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    std::error_code ec;
    root_ = fs::temp_directory_path(ec) / "fastslide_mrxs_open_errors_test";
    fs::remove_all(root_, ec);
    ASSERT_TRUE(fs::create_directories(root_, ec)) << ec.message();

    slide_file_ = root_ / "4237T1.mrxs";
    slide_dir_ = root_ / "4237T1";
    std::ofstream(slide_file_) << "mrxs stub";
    ASSERT_TRUE(fs::create_directories(slide_dir_, ec)) << ec.message();
    std::ofstream(slide_dir_ / "Slidedat.ini") << "[GENERAL]\n";
  }

  void TearDown() override {
    std::error_code ec;
    fs::permissions(slide_dir_, fs::perms::owner_all, ec);
    fs::remove_all(root_, ec);
  }

  void DenySlideDirectory() {
    std::error_code ec;
    fs::permissions(slide_dir_, fs::perms::none, ec);
    ASSERT_FALSE(ec) << "could not drop permissions: " << ec.message();
  }

  fs::path root_;
  fs::path slide_file_;
  fs::path slide_dir_;
};

TEST_F(MrxsSlideFixture, UnreadableSlideDirectoryReportsPermissionDenied) {
  if (RunningAsRoot()) {
    GTEST_SKIP() << "root bypasses permission bits";
  }
  ASSERT_NO_FATAL_FAILURE(DenySlideDirectory());

  aifocore::Status status;
  ASSERT_NO_THROW({
    auto reader_or = MrxsReader::Create(slide_file_);
    ASSERT_FALSE(reader_or.ok());
    status = reader_or.status();
  });

  EXPECT_EQ(status.code(), aifocore::StatusCode::kPermissionDenied);
}

TEST_F(MrxsSlideFixture, MissingSlidedatReportsNotFound) {
  std::error_code ec;
  fs::remove(slide_dir_ / "Slidedat.ini", ec);

  auto reader_or = MrxsReader::Create(slide_file_);

  ASSERT_FALSE(reader_or.ok());
  EXPECT_EQ(reader_or.status().code(), aifocore::StatusCode::kNotFound);
}

TEST_F(MrxsSlideFixture, MissingSlideFileReportsNotFound) {
  auto reader_or = MrxsReader::Create(root_ / "absent.mrxs");

  ASSERT_FALSE(reader_or.ok());
  EXPECT_EQ(reader_or.status().code(), aifocore::StatusCode::kNotFound);
}

TEST_F(MrxsSlideFixture, WrongExtensionReportsInvalidArgument) {
  auto reader_or = MrxsReader::Create(root_ / "4237T1.svs");

  ASSERT_FALSE(reader_or.ok());
  EXPECT_EQ(reader_or.status().code(), aifocore::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace fastslide
