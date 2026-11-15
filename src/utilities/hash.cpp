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
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/utilities/sha-256.h"

namespace fastslide {

struct QuickHashBuilder::Impl {
  Sha_256 ctx{};
  std::array<uint8_t, SIZE_OF_SHA_256_HASH> digest{};
  int64_t bytes_hashed = 0;
  bool finalized = false;
};

QuickHashBuilder::QuickHashBuilder() : impl_(std::make_unique<Impl>()) {
  sha_256_init(&impl_->ctx, impl_->digest.data());
}

QuickHashBuilder::~QuickHashBuilder() = default;

aifocore::Status QuickHashBuilder::HashFile(const fs::path& file_path) {
  if (impl_->finalized) {
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
    sha_256_write(&impl_->ctx, buffer.data(), file.gcount());
    impl_->bytes_hashed += file.gcount();
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
  if (impl_->finalized) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "Hash already finalized");
  }

  const auto closer = [](FILE* f) {
    if (f) {
      aifocore::portable_fclose(f);
    }
  };
  std::unique_ptr<FILE, decltype(closer)> file(
      aifocore::portable_fopen(file_path, "rb"), closer);
  if (!file) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Cannot open file: {}", file_path.string()));
  }

  if (aifocore::portable_fseek(file.get(), offset, SEEK_SET) != 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to seek in file");
  }

  std::array<uint8_t, 8192> buffer;
  int64_t remaining = length;

  while (remaining > 0) {
    const size_t to_read =
        std::min(remaining, static_cast<int64_t>(buffer.size()));
    const size_t bytes_read =
        aifocore::portable_fread(buffer.data(), to_read, file.get());

    if (bytes_read > 0) {
      sha_256_write(&impl_->ctx, buffer.data(), bytes_read);
      remaining -= static_cast<int64_t>(bytes_read);
      impl_->bytes_hashed += static_cast<int64_t>(bytes_read);
    }

    // A short read means the caller asked for a range the file does not
    // contain. Hashing the truncated prefix would produce a confident wrong
    // digest, so fail instead of stopping at EOF.
    if (bytes_read < to_read) {
      return AIFOCORE_MAKE_STATUS(
          ferror(file.get()) ? aifocore::StatusCode::kInternal
                             : aifocore::StatusCode::kOutOfRange,
          aifocore::fmt::format("Expected {} bytes at offset {} of {}, got {}",
                                length, offset, file_path.string(),
                                length - remaining));
    }
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status QuickHashBuilder::HashData(const uint8_t* data,
                                            size_t length) {
  if (impl_->finalized) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "Hash already finalized");
  }

  sha_256_write(&impl_->ctx, data, length);
  impl_->bytes_hashed += static_cast<int64_t>(length);
  return aifocore::Status::OkStatus();
}

aifocore::Status QuickHashBuilder::HashData(const std::vector<uint8_t>& data) {
  return HashData(data.data(), data.size());
}

int64_t QuickHashBuilder::BytesHashed() const {
  return impl_->bytes_hashed;
}

aifocore::Result<std::string> QuickHashBuilder::Finalize() {
  if (impl_->finalized) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "Hash already finalized");
  }
  if (impl_->bytes_hashed == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kFailedPrecondition,
        "Refusing to finalize a digest over zero bytes: the result would be "
        "the SHA-256 of the empty input, which is identical for every slide");
  }

  sha_256_close(&impl_->ctx);
  impl_->finalized = true;

  // Convert to hex string
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (const uint8_t byte : impl_->digest) {
    oss << std::setw(2) << static_cast<unsigned>(byte);
  }

  return oss.str();
}

}  // namespace fastslide
