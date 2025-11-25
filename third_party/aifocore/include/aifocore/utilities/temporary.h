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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_TEMPORARY_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_TEMPORARY_H_

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace aifocore::utilities {

class TemporaryDirectory {
 public:
  // Default constructor creates a temporary directory
  TemporaryDirectory() : keep_files_(false) { InitializePath(); }

  // Constructor with a flag to keep temporary files
  explicit TemporaryDirectory(bool keep_files) : keep_files_(keep_files) {
    InitializePath();
  }

  ~TemporaryDirectory() {
    if (!keep_files_) {
      try {
        fs::remove_all(
            path_.parent_path());  // Remove the entire unique directory
      } catch (const std::exception& e) {
        std::cerr << "Failed to clean up temporary folder: " << e.what()
                  << std::endl;
      }
    }
  }

  // Disable copy semantics to prevent multiple deletions of the same directory
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  // Allow move semantics
  TemporaryDirectory(TemporaryDirectory&& other) noexcept
      : path_(std::move(other.path_)), keep_files_(other.keep_files_) {}

  TemporaryDirectory& operator=(TemporaryDirectory&& other) noexcept {
    if (this != &other) {
      path_ = std::move(other.path_);
      keep_files_ = other.keep_files_;
    }
    return *this;
  }

  [[nodiscard]] const fs::path& Path() const { return path_; }

  [[nodiscard]] bool IsKept() const { return keep_files_; }

  void SetKeep(bool keep) { keep_files_ = keep; }

 private:
  fs::path path_;
  bool keep_files_;

  void InitializePath() {
    std::random_device random_device;
    std::mt19937_64 gen(random_device());  // Using 64-bit Mersenne Twister
    uint64_t unique_id = gen();

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();

    std::stringstream string_stream;
    string_stream << std::hex << std::setfill('0') << std::setw(16) << timestamp
                  << "_" << unique_id;

    // Create path with timestamp and unique identifier
    path_ = fs::temp_directory_path() / string_stream.str() / "aifocore_temp";

    // Throw if directory already exists
    if (!fs::create_directories(path_)) {
      throw std::runtime_error(
          "Failed to create temporary directory: " + path_.string() +
          " (directory may already exist)");
    }
  }
};

}  // namespace aifocore::utilities

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_TEMPORARY_H_
