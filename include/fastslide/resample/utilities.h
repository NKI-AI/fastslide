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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_UTILITIES_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_UTILITIES_H_

#include <algorithm>
#include <concepts>
#include <limits>
#include <type_traits>

namespace fastslide::resample {

template <typename T>
concept ArithmeticType = std::is_arithmetic_v<T>;

/// @brief Clamp a double back into type T's range
template <typename T>
constexpr T ClampValue(double value) noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    return static_cast<T>(value);
  } else if constexpr (std::is_unsigned_v<T>) {
    constexpr double max_val =
        static_cast<double>(std::numeric_limits<T>::max());
    return static_cast<T>(std::clamp(value, 0.0, max_val));
  } else {
    constexpr double min_val =
        static_cast<double>(std::numeric_limits<T>::min());
    constexpr double max_val =
        static_cast<double>(std::numeric_limits<T>::max());
    return static_cast<T>(std::clamp(value, min_val, max_val));
  }
}

}  // namespace fastslide::resample

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RESAMPLE_UTILITIES_H_
