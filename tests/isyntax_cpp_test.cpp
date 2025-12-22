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

#include <gtest/gtest.h>

#include "fastslide/readers/isyntax/third_party/file.h"

namespace isyntax {
namespace {

TEST(IsyntaxFileTest, OpenMissingFileFails) {
  auto result =
      IsyntaxFile::Open("this_file_should_not_exist_123456789.isyntax");
  EXPECT_FALSE(result.ok());
}

TEST(IsyntaxFileTest, EnsureThreadInitIsIdempotent) {
  IsyntaxFile::EnsureThreadInit();
  IsyntaxFile::EnsureThreadInit();
}

}  // namespace
}  // namespace isyntax
