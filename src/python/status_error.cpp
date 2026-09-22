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

#include "fastslide/python/status_error.h"

#include <nanobind/nanobind.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

#include "aifocore/status/result.h"

namespace nb = nanobind;

namespace fastslide::python {

void RegisterPythonExceptionTranslators() {
  nb::register_exception_translator(
      [](const std::exception_ptr& error, void* /*payload*/) {
        try {
          std::rethrow_exception(error);
        } catch (const NotImplementedError& e) {
          PyErr_SetString(PyExc_NotImplementedError, e.what());
        }
        // Anything else escapes this lambda, which is how nanobind is told to
        // fall through to the next translator.
      });
}

void ThrowPyErrorFromStatus(const aifocore::Status& status,
                            std::string_view context) {
  std::string message;
  if (context.empty()) {
    message = status.message();
  } else {
    message.reserve(context.size() + 2 + status.message().size());
    message.append(context).append(": ").append(status.message());
  }

  switch (status.code()) {
    case aifocore::StatusCode::kInvalidArgument:
      throw nb::value_error(message.c_str());
    case aifocore::StatusCode::kUnimplemented:
      throw NotImplementedError(message);
    default:
      throw std::runtime_error(message);
  }
}

}  // namespace fastslide::python
