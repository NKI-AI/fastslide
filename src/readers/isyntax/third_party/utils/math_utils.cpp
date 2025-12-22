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
//
// Original C code:
//   BSD 2-Clause License
//   Copyright (c) 2019-2025, Pieter Valkema

#include "fastslide/readers/isyntax/third_party/utils/math_utils.h"

#include <cctype>
#include <cstdint>

namespace isyntax {
namespace math {

const char* AtoiAndAdvance(const char* str, int32_t* dest) {
  int32_t num = 0;
  bool neg = false;

  // Skip leading whitespace
  while (std::isspace(*str)) {
    ++str;
  }

  // Check for negative sign
  if (*str == '-') {
    neg = true;
    ++str;
  }

  // Parse digits
  while (std::isdigit(*str)) {
    num = 10 * num + (*str - '0');
    ++str;
  }

  if (neg) {
    num = -num;
  }

  *dest = num;
  return str;
}

void ParseThreeIntegers(const char* str, int32_t* first, int32_t* second,
                        int32_t* third) {
  str = AtoiAndAdvance(str, first);
  str = AtoiAndAdvance(str, second);
  AtoiAndAdvance(str, third);
}

int32_t ParseUpToFiveIntegers(const char* str,
                              int32_t* array_of_five_integers) {
  for (int32_t i = 0; i < 5; ++i) {
    if (*str == '\0') {
      // Fill remaining with zeros
      for (int32_t j = i; j < 5; ++j) {
        array_of_five_integers[j] = 0;
      }
      return i;  // return number of valid integers
    }
    str = AtoiAndAdvance(str, array_of_five_integers + i);
  }
  return 5;
}

}  // namespace math
}  // namespace isyntax
