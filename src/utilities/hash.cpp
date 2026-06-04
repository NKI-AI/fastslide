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

#include "fastslide/utilities/hash.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/utilities/sha-256.h"

namespace fastslide {

QuickHashBuilder::QuickHashBuilder() : finalized_(false) {
  ctx_ = new Sha_256;
  hash_buffer_.resize(SIZE_OF_SHA_256_HASH);
  sha_256_init(static_cast<Sha_256*>(ctx_), hash_buffer_.data());
}

QuickHashBuilder::~QuickHashBuilder() {
  if (ctx_) {
    delete static_cast<Sha_256*>(ctx_);
  }
}

aifocore::Status QuickHashBuilder::HashFile(const fs::path& file_path) {
  if (finalized_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "Hash already finalized");
  }

  // Note: ifstream doesn't support wchar_t path on all platforms/compilers
  // consistently with the constructor, but on Windows MSVC it does. However,
  // standard C++17 allows fs::path. On Windows, the MSVC implementation handles
  // fs::path correctly (using wide chars). If we were using raw fopen, we'd
  // need _wfopen. std::ifstream handles fs::path natively.
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Cannot open file: {}", file_path.string()));
  }

  std::array<uint8_t, 8192> buffer;
  while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) ||
         file.gcount() > 0) {
    sha_256_write(static_cast<Sha_256*>(ctx_), buffer.data(), file.gcount());
  }

  if (file.bad()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Error reading file: {}", file_path.string()));
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status QuickHashBuilder::HashFilePart(const fs::path& file_path,
                                                int64_t offset,
                                                int64_t length) {
  if (finalized_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "Hash already finalized");
  }

  FILE* file = aifocore::portable_fopen(file_path, "rb");
  if (!file) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Cannot open file: {}", file_path.string()));
  }

  if (aifocore::portable_fseek(file, offset, SEEK_SET) != 0) {
    fclose(file);
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to seek in file");
  }

  std::array<uint8_t, 8192> buffer;
  int64_t remaining = length;

  while (remaining > 0) {
    const size_t to_read =
        std::min(remaining, static_cast<int64_t>(buffer.size()));
    const size_t bytes_read =
        aifocore::portable_fread(buffer.data(), to_read, file);

    if (bytes_read > 0) {
      sha_256_write(static_cast<Sha_256*>(ctx_), buffer.data(), bytes_read);
      remaining -= bytes_read;
    }

    if (bytes_read < to_read) {
      if (ferror(file)) {
        aifocore::portable_fclose(file);
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                    "Error reading file");
      }
      break;  // EOF
    }
  }

  aifocore::portable_fclose(file);
  return aifocore::Status::OkStatus();
}

aifocore::Status QuickHashBuilder::HashData(const uint8_t* data,
                                            size_t length) {
  if (finalized_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "Hash already finalized");
  }

  sha_256_write(static_cast<Sha_256*>(ctx_), data, length);
  return aifocore::Status::OkStatus();
}

aifocore::Status QuickHashBuilder::HashData(const std::vector<uint8_t>& data) {
  return HashData(data.data(), data.size());
}

std::string QuickHashBuilder::Finalize() {
  if (finalized_) {
    return "";  // Already finalized
  }

  sha_256_close(static_cast<Sha_256*>(ctx_));
  finalized_ = true;

  // Convert to hex string
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < SIZE_OF_SHA_256_HASH; ++i) {
    oss << std::setw(2) << static_cast<unsigned>(hash_buffer_[i]);
  }

  return oss.str();
}

}  // namespace fastslide
