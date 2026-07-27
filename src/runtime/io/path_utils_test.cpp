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

#include "fastslide/runtime/io/path_utils.h"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace fastslide::runtime::io {
namespace {

namespace fs = std::filesystem;

/// @brief Scratch bundle directory plus an out-of-bundle sibling.
class ResolveContainedPathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    base_ = fs::temp_directory_path() /
            fs::path("fastslide_path_utils_test_" +
                     std::to_string(
                         ::testing::UnitTest::GetInstance()->random_seed()) +
                     "_" + std::to_string(counter_++));
    bundle_ = base_ / "slide";
    fs::create_directories(bundle_ / "sub");
    fs::create_directories(base_ / "outside");

    std::ofstream(bundle_ / "sub" / "data.dat") << "inside";
    std::ofstream(base_ / "outside" / "secret.dat") << "outside";
  }

  void TearDown() override {
    std::error_code err;
    fs::remove_all(base_, err);
  }

  fs::path base_;
  fs::path bundle_;
  static int counter_;
};

int ResolveContainedPathTest::counter_ = 0;

TEST_F(ResolveContainedPathTest, AcceptsPathInsideBundle) {
  const auto resolved = ResolveContainedPath(bundle_, "sub/data.dat");
  ASSERT_TRUE(resolved.ok()) << resolved.status().message();
  EXPECT_EQ(resolved.value().filename(), "data.dat");
}

TEST_F(ResolveContainedPathTest, RejectsParentTraversal) {
  EXPECT_FALSE(ResolveContainedPath(bundle_, "../outside/secret.dat").ok());
  EXPECT_FALSE(ResolveContainedPath(bundle_, "sub/../../outside/x").ok());
}

TEST_F(ResolveContainedPathTest, RejectsAbsolutePath) {
  EXPECT_FALSE(ResolveContainedPath(bundle_, "/etc/passwd").ok());
}

TEST_F(ResolveContainedPathTest, RejectsEmptyPath) {
  EXPECT_FALSE(ResolveContainedPath(bundle_, "").ok());
}

/// A symlink escapes without using any `..` component, so the textual check
/// alone would let it through.
TEST_F(ResolveContainedPathTest, RejectsSymlinkPointingOutsideBundle) {
  std::error_code err;
  fs::create_symlink(base_ / "outside", bundle_ / "link", err);
  if (err) {
    GTEST_SKIP() << "symlinks unavailable: " << err.message();
  }
  EXPECT_FALSE(ResolveContainedPath(bundle_, "link/secret.dat").ok());
}

/// `slide_extra` shares a textual prefix with `slide` but is not inside it.
TEST_F(ResolveContainedPathTest, RejectsSiblingSharingNamePrefix) {
  fs::create_directories(base_ / "slide_extra");
  std::ofstream(base_ / "slide_extra" / "f.dat") << "x";
  EXPECT_FALSE(ResolveContainedPath(bundle_, "../slide_extra/f.dat").ok());
}

}  // namespace
}  // namespace fastslide::runtime::io
