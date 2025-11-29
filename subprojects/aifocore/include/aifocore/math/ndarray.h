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

/// @file ndarray.h
/// @brief N-dimensional array container with lazy expression templates.
///
/// This file provides:
/// - NDArray<T, N>: Owning container for N-dimensional arrays
/// - NDArrayView<T, N>: Non-owning view over contiguous row-major memory
/// - Expression templates for lazy evaluation of element-wise operations
///
/// Layout: All arrays use row-major (C-order) layout where the last dimension
/// is contiguous in memory.
///
/// Expression templates enable zero-copy lazy evaluation:
///   NDArray<float, 2> result = Max(a, -b) + c;
/// compiles into a single vectorizable loop with no temporaries.
///
/// Lifetime safety: Expressions and views hold raw pointers. Use ref-qualifiers
/// to prevent creating expressions/views from rvalues (which would dangle).

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_H_

#include <array>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace aifocore::math {

// Forward declarations
template <typename T, std::size_t N>
class NDArray;

template <typename T, std::size_t N>
class NDArrayView;

/// @brief Expression concept - enforces required interface for lazy evaluation.
///
/// All expressions must provide:
/// - Shape() returning const std::array<std::size_t, N>&
/// - Size() returning std::size_t (noexcept)
/// - EvalFlat(i) for O(1) element access by flat index (noexcept)
template <typename E, typename T, std::size_t N>
concept NDArrayExpressionConcept = requires(const E& expr, std::size_t i) {
  { expr.Shape() } -> std::same_as<const std::array<std::size_t, N>&>;
  { expr.Size() } noexcept -> std::same_as<std::size_t>;
  { expr.EvalFlat(i) } noexcept -> std::convertible_to<T>;
  typename E::value_type;
  { E::Rank() } noexcept -> std::same_as<std::size_t>;
};

/// @brief CRTP base class for expression templates.
///
/// Provides a unified interface for all array expressions via the derived()
/// method and forwards common operations to the derived class.
template <typename Derived>
class NDArrayExpression {
 public:
  /// @brief Access the derived expression type.
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }

  /// @brief Forward Shape() to derived class.
  auto Shape() const noexcept -> decltype(auto) { return derived().Shape(); }

  /// @brief Forward Size() to derived class.
  auto Size() const noexcept { return derived().Size(); }

  /// @brief Forward EvalFlat() to derived class for flat index evaluation.
  template <typename... Args>
  auto EvalFlat(Args&&... args) const noexcept -> decltype(auto) {
    return derived().EvalFlat(std::forward<Args>(args)...);
  }
};

/// @brief Unary negation expression for lazy element-wise negation.
///
/// Stores a raw pointer to source data and the shape. Caller must ensure the
/// source data outlives this expression (enforced via ref-qualifiers on
/// operator-).
template <typename T, std::size_t N>
class NDArrayUnaryExpr : public NDArrayExpression<NDArrayUnaryExpr<T, N>> {
  static_assert(N > 0, "Expression dimension must be positive");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using index_type = std::array<size_type, N>;

  /// @brief Construct unary negation expression.
  /// @param data Pointer to source data (must outlive this expression).
  /// @param shape Shape of the array.
  /// @param size Total number of elements (cached for O(1) Size()).
  NDArrayUnaryExpr(const T* data, const index_type& shape,
                   size_type size) noexcept
      : data_(data), shape_(shape), size_(size) {}

  const index_type& Shape() const noexcept { return shape_; }

  size_type Size() const noexcept { return size_; }

  static constexpr size_type Rank() noexcept { return N; }

  /// @brief Evaluate element at flat index (fast path for vectorization).
  T EvalFlat(size_type i) const noexcept { return -data_[i]; }

  /// @brief Evaluate element at multi-dimensional index (slower, for
  /// compatibility).
  T At(const index_type& idx) const {
    size_type linear_idx = 0;
    size_type stride = 1;
    for (std::size_t i = N; i-- > 0;) {
      if (idx[i] >= shape_[i]) {
        throw std::out_of_range("Expression index out of bounds");
      }
      linear_idx += idx[i] * stride;
      stride *= shape_[i];
    }
    return -data_[linear_idx];
  }

 private:
  const T* data_;  // Non-owning: caller ensures lifetime
  index_type shape_;
  size_type size_;  // Cached for O(1) access
};

/// @brief Expression storage policy: determines how to store expression
/// operands.
///
/// Terminal nodes (NDArray, NDArrayView) are heavy and stored by const
/// reference. Expression nodes (UnaryExpr, BinaryExpr) are lightweight and
/// stored by value.
template <typename E>
struct ExprStorage {
  using type = E;  // Default: store lightweight expressions by value
};

template <typename T, std::size_t N>
struct ExprStorage<NDArray<T, N>> {
  using type = const NDArray<T, N>&;  // Heavy: store by const reference
};

template <typename T, std::size_t N>
struct ExprStorage<NDArrayView<T, N>> {
  using type =
      NDArrayView<T, N>;  // Views are already lightweight (just pointers)
};

template <typename E>
using ExprStorage_t = typename ExprStorage<E>::type;

/// @brief Binary operation functors for element-wise operations.
struct MaxOp {
  template <typename T>
  constexpr T operator()(const T& a, const T& b) const noexcept {
    return a > b ? a : b;
  }
};

struct MinOp {
  template <typename T>
  constexpr T operator()(const T& a, const T& b) const noexcept {
    return a < b ? a : b;
  }
};

struct AddOp {
  template <typename T>
  constexpr T operator()(const T& a, const T& b) const noexcept {
    return a + b;
  }
};

struct SubOp {
  template <typename T>
  constexpr T operator()(const T& a, const T& b) const noexcept {
    return a - b;
  }
};

struct MulOp {
  template <typename T>
  constexpr T operator()(const T& a, const T& b) const noexcept {
    return a * b;
  }
};

/// @brief Binary expression template for element-wise array-array operations.
///
/// Lazily evaluates binary operations like Max, Add, Sub, Mul between two
/// array expressions. Uses ExprStorage policy: heavy containers by const&,
/// lightweight expressions by value (zero-copy chaining).
template <typename T, std::size_t N, typename Op, typename LHS, typename RHS>
class NDArrayBinaryExpr
    : public NDArrayExpression<NDArrayBinaryExpr<T, N, Op, LHS, RHS>> {
  static_assert(N > 0, "Expression dimension must be positive");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using index_type = std::array<size_type, N>;
  using LhsStore = ExprStorage_t<LHS>;
  using RhsStore = ExprStorage_t<RHS>;

  /// @brief Construct binary expression.
  /// @param lhs Left operand expression.
  /// @param rhs Right operand expression.
  /// @param op Binary operation functor.
  /// @throws std::invalid_argument if operand shapes don't match.
  NDArrayBinaryExpr(const LHS& lhs, const RHS& rhs, Op op = Op{})
      : lhs_(lhs),
        rhs_(rhs),
        op_(std::move(op)),
        shape_(GetShape(lhs_)),
        size_(GetSize(lhs_)) {
    // Validate shapes match at runtime
    if (GetShape(lhs_) != GetShape(rhs_)) {
      throw std::invalid_argument("Binary operation requires matching shapes");
    }
  }

  const index_type& Shape() const noexcept { return shape_; }

  size_type Size() const noexcept { return size_; }

  static constexpr size_type Rank() noexcept { return N; }

  /// @brief Evaluate element at flat index.
  T EvalFlat(size_type i) const noexcept {
    return op_(GetEvalFlat(lhs_, i), GetEvalFlat(rhs_, i));
  }

 private:
  LhsStore lhs_;  // Policy: const& for NDArray, value for expressions
  RhsStore rhs_;
  Op op_;
  index_type shape_;
  size_type size_;

  // Helpers to work uniformly with both references and values
  template <typename E>
  static const index_type& GetShape(const E& expr) {
    return expr.Shape();
  }

  template <typename E>
  static size_type GetSize(const E& expr) {
    return expr.Size();
  }

  template <typename E>
  static T GetEvalFlat(const E& expr, size_type i) noexcept {
    return expr.EvalFlat(i);
  }
};

/// @brief Scalar binary expression for element-wise array-scalar operations.
///
/// Lazily evaluates operations like Max(array, scalar) or Min(array, scalar).
/// Uses ExprStorage policy for zero-copy operation.
template <typename T, std::size_t N, typename Op, typename Expr>
class NDArrayScalarExpr
    : public NDArrayExpression<NDArrayScalarExpr<T, N, Op, Expr>> {
  static_assert(N > 0, "Expression dimension must be positive");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using index_type = std::array<size_type, N>;
  using ExprStore = ExprStorage_t<Expr>;

  /// @brief Construct scalar binary expression.
  /// @param expr Array expression operand.
  /// @param scalar Scalar value operand.
  /// @param op Binary operation functor.
  NDArrayScalarExpr(const Expr& expr, T scalar, Op op = Op{})
      : expr_(expr),
        scalar_(scalar),
        op_(std::move(op)),
        shape_(GetShape(expr_)),
        size_(GetSize(expr_)) {}

  const index_type& Shape() const noexcept { return shape_; }

  size_type Size() const noexcept { return size_; }

  static constexpr size_type Rank() noexcept { return N; }

  /// @brief Evaluate element at flat index.
  T EvalFlat(size_type i) const noexcept {
    return op_(GetEvalFlat(expr_, i), scalar_);
  }

 private:
  ExprStore expr_;  // Policy: const& for NDArray, value for expressions
  T scalar_;
  Op op_;
  index_type shape_;
  size_type size_;

  // Helpers to work uniformly with both references and values
  template <typename E>
  static const index_type& GetShape(const E& expr) {
    return expr.Shape();
  }

  template <typename E>
  static size_type GetSize(const E& expr) {
    return expr.Size();
  }

  template <typename E>
  static T GetEvalFlat(const E& expr, size_type i) noexcept {
    return expr.EvalFlat(i);
  }
};

/// @brief Bounds checking policy for debug vs release builds.
namespace detail {
template <bool Enable>
struct BoundsChecker {
  template <typename IndexType, std::size_t N>
  static void Check(const IndexType& idxs, const IndexType& shape) {
    for (std::size_t i = 0; i < N; ++i) {
      if (idxs[i] >= shape[i]) {
        throw std::out_of_range("NDArray index out of bounds");
      }
    }
  }
};

template <>
struct BoundsChecker<false> {
  template <typename IndexType, std::size_t N>
  static void Check(const IndexType&, const IndexType&) noexcept {}
};
}  // namespace detail

/// @brief Non-owning view over contiguous row-major N-dimensional array.
///
/// ASSUMPTIONS:
/// - Data is contiguous in memory (row-major/C-order layout).
/// - Last dimension has stride 1 (no padding between elements).
/// - Caller ensures data pointer outlives this view.
///
/// GUARANTEES:
/// - Zero-copy access to external memory.
/// - Same indexing interface as NDArray.
/// - Can create lazy expressions via operator-, etc.
///
/// For arbitrary strides, use a future NDArrayStridedView class.
template <typename T, std::size_t N>
class NDArrayView : public NDArrayExpression<NDArrayView<T, N>> {
  static_assert(N > 0, "NDArrayView dimension must be positive");

 public:
  using value_type = std::remove_const_t<T>;
  using size_type = std::size_t;
  using index_type = std::array<size_type, N>;

  /// @brief Construct a non-owning view.
  /// @param data Pointer to contiguous row-major data (must outlive view).
  /// @param shape Shape of the N-dimensional array.
  NDArrayView(T* data, const index_type& shape)
      : data_(data),
        shape_(shape),
        strides_(ComputeStrides(shape_)),
        size_(ComputeSize(shape_)) {}

  // Shape and size accessors
  const index_type& Shape() const noexcept { return shape_; }

  const index_type& Strides() const noexcept { return strides_; }

  size_type Size() const noexcept { return size_; }

  bool Empty() const noexcept { return size_ == 0; }

  static constexpr size_type Rank() noexcept { return N; }

  // Flat data access
  T* Data() noexcept { return data_; }

  const T* Data() const noexcept { return data_; }

  /// @brief Unchecked flat data access for hot loops.
  T* DataUnsafe() noexcept { return data_; }

  const T* DataUnsafe() const noexcept { return data_; }

  /// @brief Flat element access for expression evaluation.
  value_type EvalFlat(size_type i) const noexcept { return data_[i]; }

  /// @brief Multidimensional element access.
  /// Checked in debug builds, unchecked in release builds.
  template <typename... Indices>
  T& operator()(Indices... idxs) {
    static_assert(sizeof...(Indices) == N,
                  "Number of indices must match array rank");
    index_type arr{static_cast<size_type>(idxs)...};
#ifndef NDEBUG
    detail::BoundsChecker<true>::Check<index_type, N>(arr, shape_);
#endif
    return data_[LinearIndexArray(arr)];
  }

  template <typename... Indices>
  const T& operator()(Indices... idxs) const {
    static_assert(sizeof...(Indices) == N,
                  "Number of indices must match array rank");
    index_type arr{static_cast<size_type>(idxs)...};
#ifndef NDEBUG
    detail::BoundsChecker<true>::Check<index_type, N>(arr, shape_);
#endif
    return data_[LinearIndexArray(arr)];
  }

  /// @brief Index via std::array - always checked.
  T& At(const index_type& idx) {
    detail::BoundsChecker<true>::Check<index_type, N>(idx, shape_);
    return data_[LinearIndexArray(idx)];
  }

  const T& At(const index_type& idx) const {
    detail::BoundsChecker<true>::Check<index_type, N>(idx, shape_);
    return data_[LinearIndexArray(idx)];
  }

  /// @brief Unchecked index access for hot loops (after external validation).
  T& UnsafeAt(const index_type& idx) noexcept {
    return data_[LinearIndexArray(idx)];
  }

  const T& UnsafeAt(const index_type& idx) const noexcept {
    return data_[LinearIndexArray(idx)];
  }

  /// @brief Unary negation operator (lvalue-only).
  /// @return Lazy expression representing element-wise negation.
  NDArrayUnaryExpr<value_type, N> operator-() const& noexcept {
    return NDArrayUnaryExpr<value_type, N>(data_, shape_, size_);
  }

  // Disallow on rvalues to avoid dangling pointers.
  NDArrayUnaryExpr<value_type, N> operator-() const&& = delete;

 private:
  T* data_;
  index_type shape_{};
  index_type strides_{};
  size_type size_;

  static size_type ComputeSize(const index_type& shape) noexcept {
    size_type size = 1;
    for (size_type dim : shape) {
      size *= dim;
    }
    return size;
  }

  static index_type ComputeStrides(const index_type& shape) noexcept {
    index_type strides{};
    size_type acc = 1;
    // Row-major: last dimension is contiguous (stride 1)
    for (std::size_t i = N; i-- > 0;) {
      strides[i] = acc;
      acc *= shape[i];
    }
    return strides;
  }

  size_type LinearIndexArray(const index_type& idxs) const noexcept {
    size_type off = 0;
    for (std::size_t i = 0; i < N; ++i) {
      off += idxs[i] * strides_[i];
    }
    return off;
  }
};

/// @brief Owning container for N-dimensional arrays with expression template
/// support.
///
/// STORAGE:
/// - Owns data in a contiguous std::vector<T> (row-major layout).
/// - Last dimension is contiguous (stride 1).
///
/// GUARANTEES:
/// - Move operations are noexcept; moved-from arrays are valid but unspecified.
/// - Expression assignment uses single vectorizable loop (EvalFlat).
/// - Bounds checking in At() (always) and operator() (debug only).
///
/// EXAMPLE:
///   NDArray<float, 2> a({100, 100}, 1.0f);
///   NDArray<float, 2> b = Max(a, -a);  // Lazy evaluation, no temporaries
template <typename T, std::size_t N>
class NDArray : public NDArrayExpression<NDArray<T, N>> {
  static_assert(N > 0, "NDArray dimension must be positive");
  static_assert(std::is_default_constructible_v<T>,
                "Element type must be default constructible");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using index_type = std::array<size_type, N>;

  NDArray() = default;

  /// @brief Construct array with given shape (zero-initialized).
  explicit NDArray(const index_type& shape)
      : shape_(shape),
        strides_(ComputeStrides(shape_)),
        data_(ComputeSize(shape_)) {}

  /// @brief Construct array with given shape and initial value.
  NDArray(const index_type& shape, const T& init_value)
      : shape_(shape),
        strides_(ComputeStrides(shape_)),
        data_(ComputeSize(shape_), init_value) {}

  /// @brief Construct from expression template (triggers flat evaluation).
  /// Constrained to prevent collision with copy constructor.
  template <typename Expr>

    requires NDArrayExpressionConcept<Expr, T, N> &&
                 (!std::is_same_v<std::decay_t<Expr>, NDArray>)
  // NOLINTNEXTLINE(runtime/explicit)
  NDArray(const Expr& expr)
      : shape_(expr.Shape()),
        strides_(ComputeStrides(shape_)),
        data_(expr.Size()) {
    EvaluateExpression(expr);
  }

  // Rule of five with defaults (data_ manages ownership)
  NDArray(const NDArray& other) = default;
  NDArray(NDArray&& other) noexcept = default;
  NDArray& operator=(const NDArray& other) = default;
  NDArray& operator=(NDArray&& other) noexcept = default;
  ~NDArray() = default;

  /// @brief Assign from expression template (triggers flat evaluation).
  template <typename Expr>

    requires NDArrayExpressionConcept<Expr, T, N>
  NDArray& operator=(const Expr& expr) {
    shape_ = expr.Shape();
    strides_ = ComputeStrides(shape_);
    data_.resize(expr.Size());
    EvaluateExpression(expr);
    return *this;
  }

  // Shape and size accessors
  const index_type& Shape() const noexcept { return shape_; }

  const index_type& Strides() const noexcept { return strides_; }

  size_type Size() const noexcept { return data_.size(); }

  bool Empty() const noexcept { return data_.empty(); }

  static constexpr size_type Rank() noexcept { return N; }

  // Flat data access
  T* Data() noexcept { return data_.data(); }

  const T* Data() const noexcept { return data_.data(); }

  /// @brief Unchecked flat data access for hot loops.
  T* DataUnsafe() noexcept { return data_.data(); }

  const T* DataUnsafe() const noexcept { return data_.data(); }

  /// @brief Flat element access for expression evaluation.
  T EvalFlat(size_type i) const noexcept { return data_[i]; }

  /// @brief Multidimensional element access.
  /// Checked in debug builds, unchecked in release builds.
  template <typename... Indices>
  T& operator()(Indices... idxs) {
    static_assert(sizeof...(Indices) == N,
                  "Number of indices must match array rank");
    index_type arr{static_cast<size_type>(idxs)...};
#ifndef NDEBUG
    detail::BoundsChecker<true>::Check<index_type, N>(arr, shape_);
#endif
    return data_[LinearIndexArray(arr)];
  }

  template <typename... Indices>
  const T& operator()(Indices... idxs) const {
    static_assert(sizeof...(Indices) == N,
                  "Number of indices must match array rank");
    index_type arr{static_cast<size_type>(idxs)...};
#ifndef NDEBUG
    detail::BoundsChecker<true>::Check<index_type, N>(arr, shape_);
#endif
    return data_[LinearIndexArray(arr)];
  }

  /// @brief Index via std::array - always checked.
  T& At(const index_type& idx) {
    detail::BoundsChecker<true>::Check<index_type, N>(idx, shape_);
    return data_[LinearIndexArray(idx)];
  }

  const T& At(const index_type& idx) const {
    detail::BoundsChecker<true>::Check<index_type, N>(idx, shape_);
    return data_[LinearIndexArray(idx)];
  }

  /// @brief Unchecked index access for hot loops (after external validation).
  T& UnsafeAt(const index_type& idx) noexcept {
    return data_[LinearIndexArray(idx)];
  }

  const T& UnsafeAt(const index_type& idx) const noexcept {
    return data_[LinearIndexArray(idx)];
  }

  /// @brief Create a non-owning view of this array (lvalue-only).
  /// @return View that shares data with this array.
  NDArrayView<T, N> View() & noexcept {
    return NDArrayView<T, N>(data_.data(), shape_);
  }

  NDArrayView<const T, N> View() const& noexcept {
    return NDArrayView<const T, N>(data_.data(), shape_);
  }

  // Disallow on rvalues to avoid dangling views.
  NDArrayView<T, N> View() && = delete;
  NDArrayView<const T, N> View() const&& = delete;

  /// @brief Unary negation operator (lvalue-only).
  /// @return Lazy expression representing element-wise negation.
  NDArrayUnaryExpr<T, N> operator-() const& noexcept {
    return NDArrayUnaryExpr<T, N>(data_.data(), shape_, data_.size());
  }

  // Disallow on rvalues to avoid dangling pointers in expression.
  NDArrayUnaryExpr<T, N> operator-() const&& = delete;

 private:
  index_type shape_{};
  index_type strides_{};
  std::vector<T> data_{};

  static size_type ComputeSize(const index_type& shape) noexcept {
    size_type size = 1;
    for (size_type dim : shape) {
      size *= dim;
    }
    return size;
  }

  static index_type ComputeStrides(const index_type& shape) noexcept {
    index_type strides{};
    size_type acc = 1;
    // Row-major: last dimension is contiguous (stride 1)
    for (std::size_t i = N; i-- > 0;) {
      strides[i] = acc;
      acc *= shape[i];
    }
    return strides;
  }

  size_type LinearIndexArray(const index_type& idxs) const noexcept {
    size_type off = 0;
    for (std::size_t i = 0; i < N; ++i) {
      off += idxs[i] * strides_[i];
    }
    return off;
  }

  /// @brief Evaluate expression into this array using flat iteration.
  /// Single contiguous loop - cache-friendly and auto-vectorizable.
  template <typename Expr>
  void EvaluateExpression(const Expr& expr) {
    size_type size = expr.Size();
    T* dst = data_.data();
    for (size_type i = 0; i < size; ++i) {
      dst[i] = expr.EvalFlat(i);
    }
  }
};

}  // namespace aifocore::math

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_MATH_NDARRAY_H_
