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

#ifndef SIMPLETIFF_RESULT_H_
#define SIMPLETIFF_RESULT_H_

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace simpletiff {

/// Error information for Result type
struct Error {
  std::string message;

  explicit Error(std::string msg) : message(std::move(msg)) {}

  /// Create error from exception
  static Error FromException(const std::exception& e) {
    return Error(e.what());
  }
};

/// Simple Result<T> type for error handling without exceptions
///
/// This eliminates the mixed bool/exception pattern in the API.
/// Usage:
///   Result<int> DoSomething() {
///     if (error) return Error("failed");
///     return 42;  // Success
///   }
///
///   auto result = DoSomething();
///   if (!result) {
///     std::cerr << result.error().message << "\n";
///     return;
///   }
///   int value = *result;  // or result.value()
///
template <typename T>
class Result {
 public:
  /// Construct successful result
  Result(T value) : data_(std::move(value)) {}

  /// Construct error result
  Result(Error error) : data_(std::move(error)) {}

  /// Check if result contains a value
  bool ok() const { return std::holds_alternative<T>(data_); }

  /// Check if result contains an error
  bool is_error() const { return std::holds_alternative<Error>(data_); }

  /// Conversion to bool (true if ok)
  explicit operator bool() const { return ok(); }

  /// Get value (throws if error)
  T& value() & {
    if (is_error()) {
      throw std::runtime_error("Result contains error: " + error().message);
    }
    return std::get<T>(data_);
  }

  const T& value() const& {
    if (is_error()) {
      throw std::runtime_error("Result contains error: " + error().message);
    }
    return std::get<T>(data_);
  }

  T&& value() && {
    if (is_error()) {
      throw std::runtime_error("Result contains error: " + error().message);
    }
    return std::move(std::get<T>(data_));
  }

  /// Get value or default
  T value_or(T default_value) const& {
    return ok() ? std::get<T>(data_) : std::move(default_value);
  }

  T value_or(T default_value) && {
    return ok() ? std::move(std::get<T>(data_)) : std::move(default_value);
  }

  /// Dereference operators (UB if error)
  T& operator*() & { return std::get<T>(data_); }

  const T& operator*() const& { return std::get<T>(data_); }

  T&& operator*() && { return std::move(std::get<T>(data_)); }

  T* operator->() { return &std::get<T>(data_); }

  const T* operator->() const { return &std::get<T>(data_); }

  /// Get error (throws if ok)
  Error& error() & {
    if (ok()) {
      throw std::runtime_error("Result contains value, not error");
    }
    return std::get<Error>(data_);
  }

  const Error& error() const& {
    if (ok()) {
      throw std::runtime_error("Result contains value, not error");
    }
    return std::get<Error>(data_);
  }

 private:
  std::variant<T, Error> data_;
};

/// Specialization for void (success/failure only)
template <>
class Result<void> {
 public:
  /// Construct successful result
  Result() : error_(std::nullopt) {}

  /// Construct error result
  Result(Error error) : error_(std::move(error)) {}

  /// Check if result is ok
  bool ok() const { return !error_.has_value(); }

  /// Check if result is error
  bool is_error() const { return error_.has_value(); }

  /// Conversion to bool (true if ok)
  explicit operator bool() const { return ok(); }

  /// Get error (throws if ok)
  Error& error() {
    if (!error_) {
      throw std::runtime_error("Result is ok, not error");
    }
    return *error_;
  }

  const Error& error() const {
    if (!error_) {
      throw std::runtime_error("Result is ok, not error");
    }
    return *error_;
  }

 private:
  std::optional<Error> error_;
};

}  // namespace simpletiff

#endif  // SIMPLETIFF_RESULT_H_
