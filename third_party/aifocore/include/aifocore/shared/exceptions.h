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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_EXCEPTIONS_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_EXCEPTIONS_H_

#include <stdexcept>
#include <string>

namespace aifocore::shared::exceptions {

// Base class for memory-related errors
class MemoryError : public std::runtime_error {
 public:
  explicit MemoryError(const std::string& message)
      : std::runtime_error(message) {}
};

// Specific out-of-memory error, subclass of MemoryError
class OutOfMemoryError : public MemoryError {
 public:
  explicit OutOfMemoryError(
      const std::string& message = "Not enough memory to append array")
      : MemoryError(message) {}
};

// For errors related to block in use
class InUseError : public std::runtime_error {
 public:
  explicit InUseError(
      const std::string& message = "Replace blocked by writer process")
      : std::runtime_error(message) {}
};

// For errors related to array sizes
class ArraySizeError : public std::runtime_error {
 public:
  explicit ArraySizeError(
      const std::string& message = "Array size exceeds chunk size")
      : std::runtime_error(message) {}
};

// For data modification errors with customizable message
class DataModifiedError : public std::runtime_error {
 public:
  explicit DataModifiedError(
      const std::string& message = "Data was modified by another process")
      : std::runtime_error(message) {}
};

}  // namespace aifocore::shared::exceptions

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_SHARED_EXCEPTIONS_H_
