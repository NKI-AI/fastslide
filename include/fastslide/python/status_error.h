// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#pragma once

#include <stdexcept>
#include <string_view>

#include "aifocore/status/result.h"

namespace fastslide::python {

/// @brief C++ carrier for Python's builtin ``NotImplementedError``.
///
/// nanobind ships helpers for ValueError, KeyError and a handful of other
/// builtins, but not for NotImplementedError. Setting the error indicator by
/// hand is not an option either: bindings such as ``read_region`` run under
/// ``nb::call_guard<nb::gil_scoped_release>``, so the Python C API is off
/// limits at the throw site. Throwing this type instead defers the conversion
/// to the translator installed by RegisterPythonExceptionTranslators(), which
/// nanobind runs at the dispatch boundary with the GIL held.
class NotImplementedError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// @brief Install the exception translators the bindings rely on.
///
/// Call once from ``NB_MODULE`` before registering any binding.
void RegisterPythonExceptionTranslators();

/// @brief Throw the C++ exception that nanobind maps to the Python exception
///        matching @p status.
///
/// The status code selects the exception type: ``kInvalidArgument`` becomes
/// ValueError, ``kUnimplemented`` becomes NotImplementedError and every other
/// code becomes RuntimeError. Since the type already conveys the code, the
/// exception text is the status message (including its stack trace) rather
/// than the code-prefixed Status::ToString().
///
/// @param status Failed status; must not be OK.
/// @param context Optional description of what was attempted, rendered as
///        ``"<context>: <status message>"``.
[[noreturn]] void ThrowPyErrorFromStatus(const aifocore::Status& status,
                                         std::string_view context = {});

}  // namespace fastslide::python
