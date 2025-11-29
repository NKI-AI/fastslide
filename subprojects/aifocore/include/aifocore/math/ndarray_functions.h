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

/// @file ndarray_functions.h
/// @brief Element-wise functions for NDArray expressions.
///
/// This file provides element-wise functions:
/// - Max: element-wise maximum (array-array and array-scalar)
/// - Min: element-wise minimum (array-array and array-scalar)
///
/// These functions return lazy expressions that are evaluated on assignment.

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_FUNCTIONS_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_FUNCTIONS_H_

#include "aifocore/math/ndarray.h"

namespace aifocore::math {

/// @brief Element-wise maximum of two array expressions.
/// @return Lazy expression representing Max(lhs[i], rhs[i]) for all i.
template <typename LHS, typename RHS>
  requires std::is_base_of_v<NDArrayExpression<LHS>, LHS> &&
           std::is_base_of_v<NDArrayExpression<RHS>, RHS>

auto Max(const LHS& lhs, const RHS& rhs) {
  using T = typename LHS::value_type;
  static constexpr std::size_t N = LHS::Rank();
  static_assert(std::is_same_v<T, typename RHS::value_type>,
                "Operands must have same value_type");
  static_assert(N == RHS::Rank(), "Operands must have same rank");
  return NDArrayBinaryExpr<T, N, MaxOp, LHS, RHS>(lhs, rhs);
}

/// @brief Element-wise maximum of array expression and scalar.
/// @return Lazy expression representing Max(expr[i], scalar) for all i.
template <typename Expr>
  requires std::is_base_of_v<NDArrayExpression<Expr>, Expr>

auto Max(const Expr& expr, typename Expr::value_type scalar) {
  using T = typename Expr::value_type;
  static constexpr std::size_t N = Expr::Rank();
  return NDArrayScalarExpr<T, N, MaxOp, Expr>(expr, scalar);
}

/// @brief Element-wise maximum of scalar and array expression.
template <typename Expr>
  requires std::is_base_of_v<NDArrayExpression<Expr>, Expr>

auto Max(typename Expr::value_type scalar, const Expr& expr) {
  return Max(expr, scalar);
}

/// @brief Element-wise minimum of two array expressions.
/// @return Lazy expression representing Min(lhs[i], rhs[i]) for all i.
template <typename LHS, typename RHS>
  requires std::is_base_of_v<NDArrayExpression<LHS>, LHS> &&
           std::is_base_of_v<NDArrayExpression<RHS>, RHS>

auto Min(const LHS& lhs, const RHS& rhs) {
  using T = typename LHS::value_type;
  static constexpr std::size_t N = LHS::Rank();
  static_assert(std::is_same_v<T, typename RHS::value_type>,
                "Operands must have same value_type");
  static_assert(N == RHS::Rank(), "Operands must have same rank");
  return NDArrayBinaryExpr<T, N, MinOp, LHS, RHS>(lhs, rhs);
}

/// @brief Element-wise minimum of array expression and scalar.
template <typename Expr>
  requires std::is_base_of_v<NDArrayExpression<Expr>, Expr>

auto Min(const Expr& expr, typename Expr::value_type scalar) {
  using T = typename Expr::value_type;
  static constexpr std::size_t N = Expr::Rank();
  return NDArrayScalarExpr<T, N, MinOp, Expr>(expr, scalar);
}

/// @brief Element-wise minimum of scalar and array expression.
template <typename Expr>
  requires std::is_base_of_v<NDArrayExpression<Expr>, Expr>

auto Min(typename Expr::value_type scalar, const Expr& expr) {
  return Min(expr, scalar);
}

}  // namespace aifocore::math

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_FUNCTIONS_H_
