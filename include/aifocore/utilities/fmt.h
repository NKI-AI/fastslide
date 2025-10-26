// Copyright 2024 Jonas Teuwen. All Rights Reserved.
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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_FMT_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_FMT_H_

// std::format requires Clang 17+ with libc++
#if defined(__clang__) && (__clang_major__ >= 17) && __cplusplus >= 202002L && \
    defined(_LIBCPP_VERSION)
#include <format>

namespace aifocore::fmt {
using std::format;
}
#else
#include <fmt/core.h>

namespace aifocore::fmt {
using ::fmt::format;  // Explicit namespace for fmt to avoid conflicts
}
#endif

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_FMT_H_
