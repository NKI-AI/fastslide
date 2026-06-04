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
#include "fastslide/readers/isyntax/third_party/open.h"

#include <string>

#include "gtest/gtest.h"

namespace {

TEST(IsyntaxOpenCppTest, BuildXmlDumpFilename_ReplacesIsyntaxExtension) {
  EXPECT_EQ(isyntax::BuildXmlDumpFilename("foo.isyntax"), "foo.xml");
  EXPECT_EQ(isyntax::BuildXmlDumpFilename("a/b/c.isyntax"), "a/b/c.xml");
  EXPECT_EQ(isyntax::BuildXmlDumpFilename("a.b.c.isyntax"), "a.b.c.xml");
}

TEST(IsyntaxOpenCppTest,
     BuildXmlDumpFilename_AppendsXmlWhenNoIsyntaxExtension) {
  EXPECT_EQ(isyntax::BuildXmlDumpFilename("foo"), "foo.xml");
  EXPECT_EQ(isyntax::BuildXmlDumpFilename("foo.isyntax.bak"),
            "foo.isyntax.bak.xml");
}

TEST(IsyntaxOpenCppTest, BuildXmlDumpFilename_ReplacesSpacesWithUnderscores) {
  EXPECT_EQ(isyntax::BuildXmlDumpFilename("my file.isyntax"), "my_file.xml");
  EXPECT_EQ(isyntax::BuildXmlDumpFilename("/tmp/my file"), "/tmp/my_file.xml");
}

}  // namespace
