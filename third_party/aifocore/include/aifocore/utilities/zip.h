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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_ZIP_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_ZIP_H_

#include <mz.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <exception>
#include <string>
#include <vector>

namespace aifocore::utilities {

class ZipArchive {
 public:
  explicit ZipArchive(const std::string& zip_path) {
    // Create OS stream
    stream_ = mz_stream_os_create();
    if (!stream_) {
      throw std::runtime_error("Failed to create stream");
    }

    // Open the file
    if (mz_stream_os_open(stream_, zip_path.c_str(), MZ_OPEN_MODE_READ) !=
        MZ_OK) {
      mz_stream_os_delete(&stream_);
      throw std::runtime_error("Failed to open zip file: " + zip_path);
    }

    // Create zip handle
    handle_ = mz_zip_create();
    if (!handle_) {
      mz_stream_os_close(stream_);
      mz_stream_os_delete(&stream_);
      throw std::runtime_error("Failed to create zip handle");
    }

    // Open zip archive
    if (mz_zip_open(handle_, stream_, MZ_OPEN_MODE_READ) != MZ_OK) {
      mz_zip_delete(&handle_);
      mz_stream_os_close(stream_);
      mz_stream_os_delete(&stream_);
      throw std::runtime_error("Failed to open zip archive");
    }
  }

  ~ZipArchive() {
    if (handle_) {
      mz_zip_close(handle_);
      mz_zip_delete(&handle_);
    }
    if (stream_) {
      mz_stream_os_close(stream_);
      mz_stream_os_delete(&stream_);
    }
  }

  // Disable copy semantics
  ZipArchive(const ZipArchive&) = delete;
  ZipArchive& operator=(const ZipArchive&) = delete;

  // Allow move semantics
  ZipArchive(ZipArchive&& other) noexcept
      : handle_(other.handle_), stream_(other.stream_) {
    other.handle_ = nullptr;
    other.stream_ = nullptr;
  }

  ZipArchive& operator=(ZipArchive&& other) noexcept {
    if (this != &other) {
      // Clean up existing resources
      if (handle_) {
        mz_zip_close(handle_);
        mz_zip_delete(&handle_);
      }
      if (stream_) {
        mz_stream_os_close(stream_);
        mz_stream_os_delete(&stream_);
      }

      // Take ownership of other's resources
      handle_ = other.handle_;
      stream_ = other.stream_;
      other.handle_ = nullptr;
      other.stream_ = nullptr;
    }
    return *this;
  }

  std::string ReadFile(const std::string& file_name) const {
    if (mz_zip_locate_entry(handle_, file_name.c_str(), 0) != MZ_OK) {
      throw std::runtime_error("Failed to locate file '" + file_name +
                               "' in zip archive");
    }

    mz_zip_file* file_info = nullptr;
    if (mz_zip_entry_get_info(handle_, &file_info) != MZ_OK) {
      throw std::runtime_error("Failed to get file info for '" + file_name +
                               "'");
    }

    if (mz_zip_entry_read_open(handle_, 0, nullptr) != MZ_OK) {
      throw std::runtime_error("Failed to open file '" + file_name +
                               "' for reading");
    }

    std::string content(file_info->uncompressed_size, '\0');
    int32_t bytes_read =
        mz_zip_entry_read(handle_, content.data(), content.size());
    mz_zip_entry_close(handle_);

    if (bytes_read < 0 ||
        static_cast<int64_t>(bytes_read) != file_info->uncompressed_size) {
      throw std::runtime_error("Failed to read complete file '" + file_name +
                               "'");
    }

    return content;
  }

 private:
  void* handle_ = nullptr;  // mz_zip handle
  void* stream_ = nullptr;  // mz_stream handle
};

}  // namespace aifocore::utilities

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_ZIP_H_
