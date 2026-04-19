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

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_STATUS_RESULT_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_STATUS_RESULT_H_

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace aifocore {

enum class StatusCode {
  kOk = 0,
  kCancelled = 1,
  kUnknown = 2,
  kInvalidArgument = 3,
  kDeadlineExceeded = 4,
  kNotFound = 5,
  kAlreadyExists = 6,
  kPermissionDenied = 7,
  kResourceExhausted = 8,
  kFailedPrecondition = 9,
  kAborted = 10,
  kOutOfRange = 11,
  kUnimplemented = 12,
  kInternal = 13,
  kUnavailable = 14,
  kDataLoss = 15,
  kUnauthenticated = 16,
};

inline std::string StatusCodeToString(StatusCode code) {
  switch (code) {
    case StatusCode::kOk:
      return "OK";
    case StatusCode::kCancelled:
      return "CANCELLED";
    case StatusCode::kUnknown:
      return "UNKNOWN";
    case StatusCode::kInvalidArgument:
      return "INVALID_ARGUMENT";
    case StatusCode::kDeadlineExceeded:
      return "DEADLINE_EXCEEDED";
    case StatusCode::kNotFound:
      return "NOT_FOUND";
    case StatusCode::kAlreadyExists:
      return "ALREADY_EXISTS";
    case StatusCode::kPermissionDenied:
      return "PERMISSION_DENIED";
    case StatusCode::kResourceExhausted:
      return "RESOURCE_EXHAUSTED";
    case StatusCode::kFailedPrecondition:
      return "FAILED_PRECONDITION";
    case StatusCode::kAborted:
      return "ABORTED";
    case StatusCode::kOutOfRange:
      return "OUT_OF_RANGE";
    case StatusCode::kUnimplemented:
      return "UNIMPLEMENTED";
    case StatusCode::kInternal:
      return "INTERNAL";
    case StatusCode::kUnavailable:
      return "UNAVAILABLE";
    case StatusCode::kDataLoss:
      return "DATA_LOSS";
    case StatusCode::kUnauthenticated:
      return "UNAUTHENTICATED";
    default:
      return "UNKNOWN(" + std::to_string(static_cast<int>(code)) + ")";
  }
}

class Status {
 public:
  Status() : code_(StatusCode::kOk) {}

  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status OkStatus() { return Status(); }

  bool ok() const { return code_ == StatusCode::kOk; }

  StatusCode code() const { return code_; }

  const std::string& message() const { return message_; }

  std::string ToString() const {
    if (ok())
      return "OK";
    return StatusCodeToString(code_) + ": " + message_;
  }

  void IgnoreError() const {
    // Intentional no-op to suppress unused result warnings if any
  }

  bool operator==(const Status& other) const {
    return code_ == other.code_ && message_ == other.message_;
  }

  bool operator!=(const Status& other) const { return !(*this == other); }

  friend std::ostream& operator<<(std::ostream& os, const Status& status) {
    os << status.ToString();
    return os;
  }

 private:
  StatusCode code_;
  std::string message_;
};

// Helper to create a generic error status
inline Status Error(std::string msg) {
  return Status(StatusCode::kInternal, std::move(msg));
}

// Helper macros for termination
#define AIFOCORE_CHECK(condition, message)                              \
  do {                                                                  \
    if (!(condition)) {                                                 \
      std::cerr << "Check failed: " << #condition << " - " << (message) \
                << "\n";                                                \
      std::abort();                                                     \
    }                                                                   \
  } while (0)

template <typename T>
class Result {
 public:
  using value_type = T;

  // Constructors
  // NOLINTNEXTLINE(runtime/explicit)
  Result(const T& value) : data_(value) {}

  // NOLINTNEXTLINE(runtime/explicit)
  Result(T&& value) : data_(std::move(value)) {}

  // Constructor from Status (implicit allow for return Status(...))
  // NOLINTNEXTLINE(runtime/explicit)
  Result(const Status& status) : data_(status) {
    AIFOCORE_CHECK(!status.ok(), "Result constructed with OK status");
  }

  // NOLINTNEXTLINE(runtime/explicit)
  Result(Status&& status) : data_(std::move(status)) {
    AIFOCORE_CHECK(!std::get<Status>(data_).ok(),
                   "Result constructed with OK status");
  }

  // In-place construction
  template <typename... Args>
  explicit Result(std::in_place_t, Args&&... args)
      : data_(std::in_place_type<T>, std::forward<Args>(args)...) {}

  bool ok() const { return std::holds_alternative<T>(data_); }

  // Legacy/Google Style
  bool Ok() const { return ok(); }

  bool IsError() const { return !ok(); }

  explicit operator bool() const { return ok(); }

  const T& value() const& {
    if (!ok()) {
      std::cerr << "Bad Result access: " << status().ToString() << "\n";
      std::abort();
    }
    return std::get<T>(data_);
  }

  T& value() & {
    if (!ok()) {
      std::cerr << "Bad Result access: " << status().ToString() << "\n";
      std::abort();
    }
    return std::get<T>(data_);
  }

  T&& value() && {
    if (!ok()) {
      std::cerr << "Bad Result access: " << status().ToString() << "\n";
      std::abort();
    }
    return std::move(std::get<T>(data_));
  }

  // Legacy aliases
  T& Value() & { return value(); }

  const T& Value() const& { return value(); }

  T&& Value() && { return std::move(*this).value(); }

  const Status& status() const {
    if (ok()) {
      static const Status kOk = Status::OkStatus();
      return kOk;
    }
    return std::get<Status>(data_);
  }

  const Status& error() const {
    AIFOCORE_CHECK(!ok(), "Result::error() called on OK result");
    return std::get<Status>(data_);
  }

  T ValueOr(T default_value) const& {
    if (ok())
      return std::get<T>(data_);
    return default_value;
  }

  T ValueOr(T default_value) && {
    if (ok())
      return std::move(std::get<T>(data_));
    return default_value;
  }

  // Dereference
  T& operator*() & { return value(); }

  const T& operator*() const& { return value(); }

  T&& operator*() && { return std::move(*this).value(); }

  T* operator->() { return &value(); }

  const T* operator->() const { return &value(); }

 private:
  std::variant<T, Status> data_;
};

// Specialization for void
template <>
class Result<void> {
 public:
  Result() : status_(Status::OkStatus()) {}

  // NOLINTNEXTLINE(runtime/explicit)
  Result(const Status& status) : status_(status) {}

  // NOLINTNEXTLINE(runtime/explicit)
  Result(Status&& status) : status_(std::move(status)) {}

  bool ok() const { return status_.ok(); }

  bool Ok() const { return ok(); }

  bool IsError() const { return !ok(); }

  explicit operator bool() const { return ok(); }

  const Status& status() const { return status_; }

  const Status& error() const {
    AIFOCORE_CHECK(!ok(), "Result::error() called on OK result");
    return status_;
  }

  void Value() const {
    if (!ok()) {
      std::cerr << "Bad Result access: " << status_.ToString() << "\n";
      std::abort();
    }
  }

  void IgnoreError() const {}

 private:
  Status status_;
};

namespace internal {
inline std::string FormatStackFrame(char const* function, char const* file,
                                    int line, StatusCode code,
                                    std::string_view message) {
  std::string s = "  at ";
  s.append(function);
  s.append(" (");
  s.append(file);
  s.push_back(':');
  s.append(std::to_string(line));
  s.append(") [");
  s.append(StatusCodeToString(code));
  s.append("]");
  if (!message.empty()) {
    s.append(" - ");
    s.append(message);
  }
  return s;
}

inline std::string StripStackTrace(std::string_view full_message) {
  if (auto pos = full_message.find("\n  at "); pos != std::string_view::npos) {
    return std::string(full_message.substr(0, pos));
  }
  return std::string(full_message);
}
}  // namespace internal

// AddTrace function
inline Status AddTrace(const Status& st, char const* function, char const* file,
                       int line, std::string_view message = {}) {
  if (st.ok()) {
    return st;
  }

  // Root error only
  std::string root = internal::StripStackTrace(st.message());

  // Existing frames
  std::string tail;
  if (auto pos = st.message().find("\n  at "); pos != std::string::npos) {
    tail = st.message().substr(pos);
  }

  // This new frame
  std::string frame =
      internal::FormatStackFrame(function, file, line, st.code(), message);

  // Assemble: root + old frames + this frame
  std::string out = root;
  if (!tail.empty()) {
    out += tail;
  }
  out.push_back('\n');
  out += frame;

  return Status(st.code(), std::move(out));
}

template <typename T>
inline Status AddTrace(const Result<T>& res, char const* function,
                       char const* file, int line,
                       std::string_view message = {}) {
  if (res.ok()) {
    return Status::OkStatus();
  }
  return AddTrace(res.status(), function, file, line, message);
}

// Macros
#define AIFOCORE_MAKE_STATUS(code, message_expr)                        \
  ([&]() -> ::aifocore::Status {                                        \
    auto _aifocore_status_message = (message_expr);                     \
    return ::aifocore::AddTrace(                                        \
        ::aifocore::Status((code), _aifocore_status_message), __func__, \
        __FILE__, __LINE__, _aifocore_status_message);                  \
  }())

#define AIFOCORE_RETURN_IF_ERROR(expr)                                    \
  do {                                                                    \
    const auto& _status = (expr);                                         \
    if (!_status.ok())                                                    \
      return ::aifocore::AddTrace(_status, __func__, __FILE__, __LINE__); \
  } while (0)

// Implement ASSIGN_OR_RETURN
#define AIFOCORE_ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr)           \
  auto statusor = (rexpr);                                             \
  if (!statusor.ok())                                                  \
    return ::aifocore::AddTrace(statusor.status(), __func__, __FILE__, \
                                __LINE__);                             \
  lhs = std::move(statusor).value()

#define AIFOCORE_ASSIGN_OR_RETURN(lhs, rexpr)                              \
  AIFOCORE_ASSIGN_OR_RETURN_IMPL(AIFOCORE_MACROS_UID(_status_or_val), lhs, \
                                 rexpr)

#define AIFOCORE_MACROS_concat_inner(x, y) x##y
#define AIFOCORE_MACROS_concat(x, y) AIFOCORE_MACROS_concat_inner(x, y)
#define AIFOCORE_MACROS_UID(x) AIFOCORE_MACROS_concat(x, __LINE__)

}  // namespace aifocore

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_STATUS_RESULT_H_
