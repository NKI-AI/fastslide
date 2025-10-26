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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_CONCEPTS_NUMERIC_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_CONCEPTS_NUMERIC_H_

#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <ostream>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <cereal/cereal.hpp>

namespace aifocore {

// Concept for a number which is integral or float with arithmetic operations
template <typename T>
concept GenericNumber = (std::integral<T> ||
                         std::floating_point<T>)&&requires(T a, T b) {
  { a + b } -> std::convertible_to<T>;
  { a - b } -> std::convertible_to<T>;
  { a* b } -> std::convertible_to<T>;
  { a / b } -> std::convertible_to<T>;
  { std::fmod(a, b) } -> std::convertible_to<T>;
  { static_cast<double>(a) } -> std::convertible_to<double>;
};

// Use concepts to constrain template parameters
template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

// Implementation of Size in aifocore namespace
template <GenericNumber T, std::size_t N>
class Size {
 public:
  // Constructors
  constexpr Size() : data_{} {}

  constexpr Size(std::initializer_list<T> init) {
    if (init.size() != N) {
      throw std::invalid_argument("Initializer list must have size N.");
    }
    std::copy(init.begin(), init.end(), data_.begin());
  }

  // Allow brace initialization with specific size arguments
  template <typename... Args>
  constexpr explicit Size(Args... args) requires(
      sizeof...(args) == N && (std::convertible_to<Args, T> && ...)) {
    data_ = {static_cast<T>(args)...};
  }

  // Access operators
  constexpr T& operator[](std::size_t index) { return data_[index]; }

  constexpr const T& operator[](std::size_t index) const {
    return data_[index];
  }

  // This allows structured bindings and .first/.second notation to work
  template <std::size_t M = N>
  constexpr const T& first() const requires(M == 2) {
    return data_[0];
  }

  template <std::size_t M = N>
  constexpr T& first() requires(M == 2) {
    return data_[0];
  }

  template <std::size_t M = N>
  constexpr const T& second() const requires(M == 2) {
    return data_[1];
  }

  template <std::size_t M = N>
  constexpr T& second() requires(M == 2) {
    return data_[1];
  }

  // TODO(jonasteuwen): What do we feel about this implicit type conversion?
  // Conversion to `std::array<T, N>`
  constexpr operator std::array<T, N>() const { return data_; }

  // Conversion to `std::vector<T>
  constexpr operator std::vector<T>() const {
    return {data_.begin(), data_.end()};
  }

  explicit constexpr Size(std::tuple<T, T> tuple) requires(N == 2) {
    data_[0] = std::get<0>(tuple);
    data_[1] = std::get<1>(tuple);
  }

  // Constructor from a Point (which is a std::pair) for backward compatibility
  explicit constexpr Size(const std::pair<T, T>& point) requires(N == 2) {
    data_[0] = point.first;
    data_[1] = point.second;
  }

  // Conversion operator to Size<U, N>
  template <typename U>
  explicit operator Size<U, N>() const requires std::convertible_to<T, U> {
    Size<U, N> result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] = static_cast<U>(data_[i]);  // Cast each element to U
    }
    return result;
  }

  // For backward compatibility with Point<T> (std::pair<T, T>)
  explicit operator std::pair<T, T>() const requires(N == 2) {
    return {data_[0], data_[1]};
  }

  // Addition and subtraction
  template <typename U, std::size_t M>
  constexpr Size operator+(const Size<U, M>& other) const
      requires(M == N && std::convertible_to<U, T>) {
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] = data_[i] + other[i];
    }
    return result;
  }

  template <typename U, std::size_t M>
  constexpr Size operator-(const Size<U, M>& other) const
      requires(M == N && std::convertible_to<U, T>) {
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] = data_[i] - other[i];
    }
    return result;
  }

  // Division by a scalar (double)
  constexpr Size operator/(double scalar) const {
    if (scalar == 0) {
      throw std::invalid_argument("Division by zero is not allowed.");
    }
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] =
          static_cast<T>(data_[i] / scalar);  // Cast to T after division
    }
    return result;
  }

  // Division by a Size  object
  template <typename U, std::size_t M>
  constexpr Size operator/(const Size<U, M>& other) const
      requires(M == N && std::convertible_to<U, T>) {
    // Check for division by zero in any element
    for (std::size_t i = 0; i < N; ++i) {
      if (other[i] == 0) {
        throw std::invalid_argument("Division by zero is not allowed.");
      }
    }
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] =
          static_cast<T>(data_[i] / other[i]);  // Cast to T after division
    }
    return result;
  }

  // Subtraction by a scalar (double)
  constexpr Size operator-(double scalar) const {
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] = static_cast<T>(data_[i] - scalar);
    }
    return result;
  }

  // Addition by a scalar (double)
  constexpr Size operator+(double scalar) const {
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] = static_cast<T>(data_[i] + scalar);
    }
    return result;
  }

  // Multiplication by a scalar (double)
  constexpr Size operator*(double scalar) const {
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] = static_cast<T>(data_[i] * scalar);
    }
    return result;
  }

  // Equality comparison
  template <typename U, std::size_t M>
  constexpr bool operator==(const Size<U, M>& other) const
      requires(M == N && std::convertible_to<U, T>) {
    for (std::size_t i = 0; i < N; ++i) {
      if (data_[i] != static_cast<T>(other[i])) {
        return false;
      }
    }
    return true;
  }

  template <typename U, std::size_t M>
  constexpr bool operator!=(const Size<U, M>& other) const
      requires(M == N && std::convertible_to<U, T>) {
    return !(*this == other);
  }

  // For std::cout (and other ostreams) - uses curly braces {}
  friend std::ostream& operator<<(std::ostream& os, const Size& size) {
    os << "{";
    for (std::size_t i = 0; i < N; ++i) {
      os << size[i];
      if (i < N - 1) {
        os << ", ";
      }
    }
    os << "}";
    return os;
  }

  // Add cereal serialization for Size<T, N>
  template <class Archive>
  void serialize(Archive& archive) {
    for (std::size_t i = 0; i < N; ++i) {
      archive(data_[i]);
    }
  }

  // Add structured binding support
  template <std::size_t I>
  friend constexpr const T& get(const Size<T, N>& s) noexcept {
    static_assert(I < N, "Index out of bounds");
    return s.data_[I];
  }

  template <std::size_t I>
  friend constexpr T& get(Size<T, N>& s) noexcept {
    static_assert(I < N, "Index out of bounds");
    return s.data_[I];
  }

  // Friend functions for commutative operations
  friend constexpr Size operator+(double scalar, const Size& size) {
    return size + scalar;  // Reuse the member operator
  }

  friend constexpr Size operator*(double scalar, const Size& size) {
    return size * scalar;  // Reuse the member operator
  }

  friend constexpr Size operator-(double scalar, const Size& size) {
    Size result;
    for (std::size_t i = 0; i < N; ++i) {
      result[i] = static_cast<T>(scalar - size[i]);
    }
    return result;
  }

 private:
  std::array<T, N> data_;
};

template <GenericNumber T, std::size_t N>
Size<T, N> Floor(const Size<T, N>& input) {
  Size<T, N> result;
  for (std::size_t i = 0; i < N; ++i) {
    result[i] = static_cast<T>(std::floor(static_cast<double>(input[i])));
  }
  return result;
}

template <GenericNumber T, std::size_t N>
Size<T, N> Ceil(const Size<T, N>& input) {
  Size<T, N> result;
  for (std::size_t i = 0; i < N; ++i) {
    result[i] = static_cast<T>(std::ceil(static_cast<double>(input[i])));
  }
  return result;
}

template <GenericNumber T, std::size_t N>
Size<T, N> Clamp(const Size<T, N>& value, const Size<T, N>& min_value,
                 const Size<T, N>& max_value) {
  Size<T, N> result;
  for (std::size_t i = 0; i < N; ++i) {
    result[i] = std::clamp(value[i], min_value[i], max_value[i]);
  }
  return result;
}

template <GenericNumber T, std::size_t N>
Size<T, N> Min(const Size<T, N>& a, const Size<T, N>& b) {
  Size<T, N> result;
  for (std::size_t i = 0; i < N; ++i) {
    result[i] = std::min(a[i], b[i]);
  }
  return result;
}

template <GenericNumber T, std::size_t N>
Size<T, N> Max(const Size<T, N>& a, const Size<T, N>& b) {
  Size<T, N> result;
  for (std::size_t i = 0; i < N; ++i) {
    result[i] = std::max(a[i], b[i]);
  }
  return result;
}

namespace concepts {

// Aliasing concepts to maintain backward compatibility
template <typename T>
concept GenericNumber = aifocore::GenericNumber<T>;

template <typename T>
concept Numeric = aifocore::Numeric<T>;

}  // namespace concepts
}  // namespace aifocore

// Add tuple-like interface specializations for structured bindings
// TODO(jonasteuwen): we should really not override the std:: library
namespace std {
template <typename T, std::size_t N>
struct tuple_size<aifocore::Size<T, N>>
    : std::integral_constant<std::size_t, N> {};

template <std::size_t I, typename T, std::size_t N>
struct tuple_element<I, aifocore::Size<T, N>> {
  static_assert(I < N, "Index out of bounds");
  using type = T;
};
}  // namespace std

namespace fmt {

// Specialization of `fmt::formatter` for `aifocore::Size<T, N>`
template <typename T, std::size_t N>
struct formatter<aifocore::Size<T, N>> {
  // Parse format (required by fmt)
  constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
    return ctx.begin();  // No custom format specifiers
  }

  // Format function to convert Size<T, N> into a string
  template <typename FormatContext>
  auto format(const aifocore::Size<T, N>& size, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    auto out = ctx.out();
    *out++ = '{';
    for (std::size_t i = 0; i < N; ++i) {
      out = fmt::format_to(out, "{}", size[i]);
      if (i < N - 1) {
        *out++ = ',';
        *out++ = ' ';
      }
    }
    *out++ = '}';
    return out;
  }
};

}  // namespace fmt

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_CONCEPTS_NUMERIC_H_
