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

#pragma once

#include <cstdint>
#include <string_view>

namespace isyntax {
namespace math {

/// @brief Parse an integer from a string and advance the pointer past the
/// number.
///
/// This function skips leading whitespace, handles an optional minus sign, and
/// parses digits. It returns a pointer to the first character after the parsed
/// integer.
///
/// @param str Input string to parse (may contain leading whitespace)
/// @param dest Pointer to store the parsed integer
/// @return Pointer to the first character after the parsed integer
const char* AtoiAndAdvance(const char* str, int32_t* dest);

/// @brief Parse three space-separated integers from a string.
///
/// Used for parsing dimension ranges in iSyntax XML format (e.g., "start step
/// end").
///
/// @param str Input string containing three integers
/// @param first Pointer to store the first integer
/// @param second Pointer to store the second integer
/// @param third Pointer to store the third integer
void ParseThreeIntegers(const char* str, int32_t* first, int32_t* second,
                        int32_t* third);

/// @brief Parse up to five space-separated integers from a string.
///
/// Used for parsing cluster coordinates in iSyntax XML format. Returns the
/// number of integers successfully parsed. Remaining array elements are filled
/// with zeros.
///
/// @param str Input string containing up to five integers
/// @param array_of_five_integers Array to store the parsed integers (must have
/// space for 5 elements)
/// @return Number of integers successfully parsed (0-5)
int32_t ParseUpToFiveIntegers(const char* str, int32_t* array_of_five_integers);

}  // namespace math
}  // namespace isyntax
