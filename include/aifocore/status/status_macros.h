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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_STATUS_STATUS_MACROS_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_STATUS_STATUS_MACROS_H_

#include <string>
#include <string_view>
#include <utility>
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace aifocore::status {

/**
 * @brief Formats a single stack‐frame line.
 *
 * Produces a line of the form:
 *     "  at FunctionName (file.cpp:123) [StatusCode] - optional message"
 *
 * @param function Name of the function.
 * @param file     Source file path.
 * @param line     Source line number.
 * @param code     Status code for this frame.
 * @param message  Optional message to append.
 * @return A formatted frame string.
 */
inline std::string FormatStackFrame(char const* function, char const* file,
                                    int line, absl::StatusCode code,
                                    std::string_view message) {
  std::string s = "  at ";
  s.append(function);
  s.append(" (");
  s.append(file);
  s.push_back(':');
  s.append(std::to_string(line));
  s.append(") [");

  // Use Abseil's built-in conversion
  s.append(absl::StatusCodeToString(code));

  s.append("]");

  if (!message.empty()) {
    s.append(" - ");
    s.append(message);
  }
  return s;
}

/**
 * @brief Strips any existing stack‐frame lines from a full message,
 *        leaving only the root error text.
 *
 * Removes everything from the first "\n  at " onward.
 *
 * @param full_message The full status message, possibly containing frames.
 * @return The root message without any frames.
 */
inline std::string StripStackTrace(std::string_view full_message) {
  if (auto pos = full_message.find("\n  at "); pos != std::string_view::npos) {
    return std::string(full_message.substr(0, pos));
  }
  return std::string(full_message);
}

/**
 * @brief Core helper that appends exactly one new stack frame to a Status.
 *
 * If the input status is ok(), returns it unmodified. Otherwise:
 *  1. Strips off any existing frames from the message.
 *  2. Preserves the root error text.
 *  3. Appends the new frame (with optional message).
 *
 * @param st        Original absl::Status.
 * @param function  Name of the calling function.
 * @param file      Source file path.
 * @param line      Source line number.
 * @param message   Optional per‐frame message.
 * @return A new absl::Status with the appended frame.
 */
inline absl::Status AddTraceImpl(absl::Status const& st, char const* function,
                                 char const* file, int line,
                                 std::string_view message) {
  if (st.ok()) {
    return st;
  }

  // Root error only
  std::string root = StripStackTrace(st.message());

  // Existing frames
  std::string tail;
  if (auto pos = st.message().find("\n  at "); pos != std::string::npos) {
    tail = st.message().substr(pos);
  }

  // This new frame
  std::string frame =
      FormatStackFrame(function, file, line, st.code(), message);

  // Assemble: root + old frames + this frame
  std::string out = root;
  if (!tail.empty()) {
    out += tail;
  }
  out.push_back('\n');
  out += frame;

  return absl::Status(st.code(), out);
}

/**
 * @brief Overload for absl::StatusOr<T>.
 *
 * If the input is ok(), returns it unmodified. Otherwise extracts the
 * Status and calls the Status overload.
 */
template <typename T>
inline absl::StatusOr<T> AddTraceImpl(absl::StatusOr<T> const& sor,
                                      char const* function, char const* file,
                                      int line, std::string_view message) {
  if (sor.ok()) {
    return sor;
  }
  return AddTraceImpl(sor.status(), function, file, line, message);
}

/**
 * @brief Public entrypoint for appending a trace frame to an absl::Status.
 *
 * @param st        Original status.
 * @param function  Name of the calling function.
 * @param file      Source file path.
 * @param line      Source line number.
 * @param message   Optional per‐frame message.
 * @return New status with appended frame.
 */
inline absl::Status AddTrace(absl::Status const& st, char const* function,
                             char const* file, int line,
                             std::string_view message = {}) {
  return AddTraceImpl(st, function, file, line, message);
}

/**
 * @brief Public entrypoint for appending a trace frame to a StatusOr<T>.
 *
 * @param sor       Original StatusOr<T>.
 * @param function  Name of the calling function.
 * @param file      Source file path.
 * @param line      Source line number.
 * @param message   Optional per‐frame message.
 * @return New StatusOr<T> with appended frame or the original value.
 */
template <typename T>
inline absl::StatusOr<T> AddTrace(absl::StatusOr<T> const& sor,
                                  char const* function, char const* file,
                                  int line, std::string_view message = {}) {
  return AddTraceImpl(sor, function, file, line, message);
}

}  // namespace aifocore::status

//------------------------------------------------------------------------------
// Macros
//------------------------------------------------------------------------------

/**
 * @brief Create a traced absl::Status with an initial frame.
 *
 * Expands to:
 *   return AddTrace(Status(code, message), __func__, __FILE__, __LINE__, message);
 *
 * @param code    The absl::StatusCode to use.
 * @param message The error message.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define MAKE_STATUS(code, message)                                        \
  ::aifocore::status::AddTrace(absl::Status((code), (message)), __func__, \
                               __FILE__, __LINE__, (message))

/**
 * @brief Create a traced absl::StatusOr<type> with an initial frame.
 *
 * @tparam type    The T in StatusOr<T>.
 * @param code     The absl::StatusCode to use.
 * @param message  The error message.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define MAKE_STATUSOR(type, code, message)                            \
  ::aifocore::status::AddTrace<type>(absl::Status((code), (message)), \
                                     __func__, __FILE__, __LINE__, (message))

/**
 * @brief Propagate an absl::Status, appending this function as a frame.
 *
 * If expr.ok(), continues; otherwise returns the traced status.
 *
 * @param expr  A Status‐producing expression.
 * @param msg   Optional message for this frame.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define RETURN_IF_ERROR(expr, msg)                                           \
  do {                                                                       \
    auto _st = (expr);                                                       \
    if (!_st.ok()) {                                                         \
      return ::aifocore::status::AddTrace(_st, __func__, __FILE__, __LINE__, \
                                          (msg));                            \
    }                                                                        \
  } while (0)

/**
 * @brief Unpack a StatusOr<T> into lhs or return on error with a trace.
 *
 * On error, appends this function as a frame (with optional msg) and returns.
 *
 * @param lhs   Target variable to assign.
 * @param expr  A StatusOr<T>‐producing expression.
 * @param ...   Optional message for this frame.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define ASSIGN_OR_RETURN(lhs, expr, ...)                                     \
  do {                                                                       \
    auto _sor = (expr);                                                      \
    if (!_sor.ok()) {                                                        \
      return ::aifocore::status::AddTrace(_sor.status(), __func__, __FILE__, \
                                          __LINE__, ##__VA_ARGS__);          \
    }                                                                        \
    lhs = std::move(_sor.value());                                           \
  } while (0)

/**
 * @brief Unpack a StatusOr<T> into lhs or return on error with a trace.
 * For use in functions that return StatusOr<U>.
 *
 * On error, appends this function as a frame (with msg) and returns the status.
 *
 * @param lhs   Target variable to assign.
 * @param expr  A StatusOr<T>‐producing expression.
 * @param ...   Optional message for this frame.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define ASSIGN_OR_RETURN_STATUSOR(lhs, expr, ...)                            \
  do {                                                                       \
    auto _sor = (expr);                                                      \
    if (!_sor.ok()) {                                                        \
      return ::aifocore::status::AddTrace(_sor.status(), __func__, __FILE__, \
                                          __LINE__, ##__VA_ARGS__);          \
    }                                                                        \
    lhs = std::move(_sor.value());                                           \
  } while (0)

/**
 * @brief Declare a variable and unpack a StatusOr<T> into it or return on error with a trace.
 * For use in functions that return StatusOr<U>.
 *
 * Declares the variable and then assigns the result of the expression to it.
 * On error, appends this function as a frame and returns the status.
 *
 * @param type   The type of the variable to declare.
 * @param name   The name of the variable to declare.
 * @param expr   A StatusOr<T>‐producing expression.
 * @param ...    Optional message for this frame.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define DECLARE_ASSIGN_OR_RETURN_STATUSOR(type, name, expr, ...) \
  type name;                                                     \
  ASSIGN_OR_RETURN_STATUSOR(name, expr, ##__VA_ARGS__)

/**
 * @brief Move-aware version of ASSIGN_OR_RETURN for move-only types like unique_ptr.
 *
 * Unpacks a StatusOr<T> into lhs using move semantics or returns on error with a trace.
 * This macro is designed for move-only types where the standard ASSIGN_OR_RETURN
 * would fail due to copy constructor deletion.
 *
 * @param lhs   Target variable to assign (must be already declared).
 * @param expr  A StatusOr<T>‐producing expression.
 * @param ...   Optional message for this frame.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define ASSIGN_OR_RETURN_MOVE(lhs, expr, ...)                           \
  do {                                                                  \
    auto _status_or_result = (expr);                                    \
    if (!_status_or_result.ok()) {                                      \
      return ::aifocore::status::AddTrace(_status_or_result.status(),   \
                                          __func__, __FILE__, __LINE__, \
                                          ##__VA_ARGS__);               \
    }                                                                   \
    lhs = std::move(_status_or_result).value();                         \
  } while (0)

/**
 * @brief Declare a variable and unpack a StatusOr<T> into it using move semantics.
 *
 * Combines variable declaration with move-aware assignment for move-only types.
 * On error, appends this function as a frame and returns the status.
 *
 * @param type   The type of the variable to declare.
 * @param name   The name of the variable to declare.  
 * @param expr   A StatusOr<T>‐producing expression.
 * @param ...    Optional message for this frame.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-do-while)
#define DECLARE_ASSIGN_OR_RETURN_MOVE(type, name, expr, ...) \
  type name;                                                 \
  ASSIGN_OR_RETURN_MOVE(name, expr, ##__VA_ARGS__)

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_STATUS_STATUS_MACROS_H_
