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

#include "fastslide/readers/isyntax/third_party/utils/math_utils.h"

#include <cstdint>
#include <array>

#include <gtest/gtest.h>



namespace isyntax {
namespace math {
namespace {

TEST(MathUtilsTest, AtoiAndAdvanceBasicPositive) {
  int32_t result = 0;
  const char* str = "123";
  const char* next = AtoiAndAdvance(str, &result);

  EXPECT_EQ(result, 123);
  EXPECT_EQ(*next, '\0');  // Should be at end of string
}

TEST(MathUtilsTest, AtoiAndAdvanceBasicNegative) {
  int32_t result = 0;
  const char* str = "-456";
  const char* next = AtoiAndAdvance(str, &result);

  EXPECT_EQ(result, -456);
  EXPECT_EQ(*next, '\0');
}

TEST(MathUtilsTest, AtoiAndAdvanceWithLeadingWhitespace) {
  int32_t result = 0;
  const char* str = "   789";
  const char* next = AtoiAndAdvance(str, &result);

  EXPECT_EQ(result, 789);
  EXPECT_EQ(*next, '\0');
}

TEST(MathUtilsTest, AtoiAndAdvanceWithTrailingNonDigit) {
  int32_t result = 0;
  const char* str = "123 abc";
  const char* next = AtoiAndAdvance(str, &result);

  EXPECT_EQ(result, 123);
  EXPECT_EQ(*next, ' ');  // Should stop at space
}

TEST(MathUtilsTest, AtoiAndAdvanceZero) {
  int32_t result = -1;  // Non-zero initial value
  const char* str = "0";
  const char* next = AtoiAndAdvance(str, &result);

  EXPECT_EQ(result, 0);
  EXPECT_EQ(*next, '\0');
}

TEST(MathUtilsTest, AtoiAndAdvanceMultipleInts) {
  int32_t first = 0, second = 0;
  const char* str = "42 99";

  const char* next = AtoiAndAdvance(str, &first);
  EXPECT_EQ(first, 42);

  next = AtoiAndAdvance(next, &second);
  EXPECT_EQ(second, 99);
  EXPECT_EQ(*next, '\0');
}

TEST(MathUtilsTest, ParseThreeIntegersBasic) {
  int32_t first = 0, second = 0, third = 0;
  ParseThreeIntegers("10 20 30", &first, &second, &third);

  EXPECT_EQ(first, 10);
  EXPECT_EQ(second, 20);
  EXPECT_EQ(third, 30);
}

TEST(MathUtilsTest, ParseThreeIntegersWithNegatives) {
  int32_t first = 0, second = 0, third = 0;
  ParseThreeIntegers("-5 100 -50", &first, &second, &third);

  EXPECT_EQ(first, -5);
  EXPECT_EQ(second, 100);
  EXPECT_EQ(third, -50);
}

TEST(MathUtilsTest, ParseThreeIntegersExtraWhitespace) {
  int32_t first = 0, second = 0, third = 0;
  ParseThreeIntegers("  1   2   3  ", &first, &second, &third);

  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 2);
  EXPECT_EQ(third, 3);
}

TEST(MathUtilsTest, ParseUpToFiveIntegersAllFive) {
  std::array<int32_t, 5> values = {0, 0, 0, 0, 0};
  int32_t count = ParseUpToFiveIntegers("1 2 3 4 5", values.data());

  EXPECT_EQ(count, 5);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
  EXPECT_EQ(values[4], 5);
}

TEST(MathUtilsTest, ParseUpToFiveIntegersPartial) {
  std::array<int32_t, 5> values = {-1, -1, -1, -1, -1};
  int32_t count = ParseUpToFiveIntegers("10 20 30", values.data());

  EXPECT_EQ(count, 3);
  EXPECT_EQ(values[0], 10);
  EXPECT_EQ(values[1], 20);
  EXPECT_EQ(values[2], 30);
  EXPECT_EQ(values[3], 0);  // Remaining should be zero-filled
  EXPECT_EQ(values[4], 0);
}

TEST(MathUtilsTest, ParseUpToFiveIntegersEmpty) {
  std::array<int32_t, 5> values = {-1, -1, -1, -1, -1};
  int32_t count = ParseUpToFiveIntegers("", values.data());

  EXPECT_EQ(count, 0);
  for (const auto& val : values) {
    EXPECT_EQ(val, 0);  // All should be zero-filled
  }
}

TEST(MathUtilsTest, ParseUpToFiveIntegersOne) {
  std::array<int32_t, 5> values = {0, 0, 0, 0, 0};
  int32_t count = ParseUpToFiveIntegers("42", values.data());

  EXPECT_EQ(count, 1);
  EXPECT_EQ(values[0], 42);
  EXPECT_EQ(values[1], 0);
  EXPECT_EQ(values[2], 0);
  EXPECT_EQ(values[3], 0);
  EXPECT_EQ(values[4], 0);
}

// Test use case from iSyntax: dimension range parsing
TEST(MathUtilsTest, IsyntaxDimensionRangeExample) {
  // Example from isyntax.c: parse "start step end" for dimension ranges
  int32_t start = 0, step = 0, end = 0;
  ParseThreeIntegers("0 128 1024", &start, &step, &end);

  EXPECT_EQ(start, 0);
  EXPECT_EQ(step, 128);
  EXPECT_EQ(end, 1024);
}

// Test use case from iSyntax: cluster coordinate parsing
TEST(MathUtilsTest, IsyntaxClusterCoordinateExample) {
  // Example from isyntax.c: parse up to 5 integers for cluster coordinates
  std::array<int32_t, 5> coords = {0, 0, 0, 0, 0};
  int32_t count = ParseUpToFiveIntegers("66302 66302 0 8 1", coords.data());

  EXPECT_EQ(count, 5);
  EXPECT_EQ(coords[0], 66302);  // x coordinate
  EXPECT_EQ(coords[1], 66302);  // y coordinate
  EXPECT_EQ(coords[2], 0);      // color component
  EXPECT_EQ(coords[3], 8);      // scale
  EXPECT_EQ(coords[4], 1);      // coefficient
}

}  // namespace
}  // namespace math
}  // namespace isyntax
