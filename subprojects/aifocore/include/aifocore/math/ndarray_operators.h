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

/// @file ndarray_operators.h
/// @brief Element-wise binary operators for NDArray expressions.
///
/// This file provides operator overloads for element-wise operations:
/// - operator+: element-wise addition
/// - operator-: element-wise subtraction (binary)
/// - operator*: element-wise multiplication
///
/// These operators return lazy expressions that are evaluated on assignment.

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_OPERATORS_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_OPERATORS_H_

#include "aifocore/math/ndarray.h"

namespace aifocore::math {

/// @brief Element-wise addition operator.
/// @return Lazy expression representing lhs[i] + rhs[i] for all i.
template <typename LHS, typename RHS>
requires std::is_base_of_v<NDArrayExpression<LHS>, LHS> &&
    std::is_base_of_v<NDArrayExpression<RHS>, RHS>

auto operator+(const LHS& lhs, const RHS& rhs) {
  using T = typename LHS::value_type;
  static constexpr std::size_t N = LHS::Rank();
  static_assert(std::is_same_v<T, typename RHS::value_type>,
                "Operands must have same value_type");
  static_assert(N == RHS::Rank(), "Operands must have same rank");
  return NDArrayBinaryExpr<T, N, AddOp, LHS, RHS>(lhs, rhs);
}

/// @brief Element-wise subtraction operator (binary).
/// @return Lazy expression representing lhs[i] - rhs[i] for all i.
template <typename LHS, typename RHS>
requires std::is_base_of_v<NDArrayExpression<LHS>, LHS> &&
    std::is_base_of_v<NDArrayExpression<RHS>, RHS>

auto operator-(const LHS& lhs, const RHS& rhs) {
  using T = typename LHS::value_type;
  static constexpr std::size_t N = LHS::Rank();
  static_assert(std::is_same_v<T, typename RHS::value_type>,
                "Operands must have same value_type");
  static_assert(N == RHS::Rank(), "Operands must have same rank");
  return NDArrayBinaryExpr<T, N, SubOp, LHS, RHS>(lhs, rhs);
}

/// @brief Element-wise multiplication operator.
/// @return Lazy expression representing lhs[i] * rhs[i] for all i.
template <typename LHS, typename RHS>
requires std::is_base_of_v<NDArrayExpression<LHS>, LHS> &&
    std::is_base_of_v<NDArrayExpression<RHS>, RHS>

auto operator*(const LHS& lhs, const RHS& rhs) {
  using T = typename LHS::value_type;
  static constexpr std::size_t N = LHS::Rank();
  static_assert(std::is_same_v<T, typename RHS::value_type>,
                "Operands must have same value_type");
  static_assert(N == RHS::Rank(), "Operands must have same rank");
  return NDArrayBinaryExpr<T, N, MulOp, LHS, RHS>(lhs, rhs);
}

}  // namespace aifocore::math

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_OPERATORS_H_
