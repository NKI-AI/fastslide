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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILE_HANDLE_POOL_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILE_HANDLE_POOL_H_

#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stack>
#include <string>
#include <vector>

#include "aifocore/platform/portability.h"

namespace fastslide {

class FileHandlePool {
 public:
  explicit FileHandlePool(const std::string& path) : path_(path) {}

  // Delete copy/move
  FileHandlePool(const FileHandlePool&) = delete;
  FileHandlePool& operator=(const FileHandlePool&) = delete;
  FileHandlePool(FileHandlePool&&) = delete;
  FileHandlePool& operator=(FileHandlePool&&) = delete;

  ~FileHandlePool() {
    while (!handles_.empty()) {
      int fd = handles_.top();
      handles_.pop();
      if (fd != -1) {
        aifocore::portable_close(fd);
      }
    }
  }

  int GetHandle() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handles_.empty()) {
      int fd = handles_.top();
      handles_.pop();
      return fd;
    }

    // Create new handle
    int fd = aifocore::portable_open(path_.c_str(), O_RDONLY | O_BINARY);
    return fd;
  }

  void ReturnHandle(int fd) {
    if (fd == -1)
      return;
    std::lock_guard<std::mutex> lock(mutex_);
    handles_.push(fd);
  }

 private:
  std::string path_;
  std::stack<int> handles_;
  std::mutex mutex_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILE_HANDLE_POOL_H_
