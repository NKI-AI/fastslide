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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_BINARY_UTILS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_BINARY_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "absl/status/statusor.h"

/**
 * @file binary_utils.h
 * @brief Binary I/O utility functions
 * 
 * Provides common binary I/O operations like reading integers in specific
 * endianness and decompressing data.
 */

namespace fastslide {
namespace runtime {
namespace io {

/// @brief Read a little-endian 32-bit integer from a file
/// @param file File pointer (must be open for reading)
/// @return 32-bit integer value or error
/// @note Does not change file pointer on error
absl::StatusOr<int32_t> ReadLeInt32(FILE* file);

/// @brief Read a little-endian 32-bit unsigned integer from a file
/// @param file File pointer (must be open for reading)
/// @return 32-bit unsigned integer value or error
/// @note Does not change file pointer on error
absl::StatusOr<uint32_t> ReadLeUInt32(FILE* file);

/// @brief Decompress zlib-compressed data
/// @param data Pointer to compressed data
/// @param compressed_size Size of compressed data in bytes
/// @param expected_size Expected size of decompressed data in bytes
/// @return Decompressed data or error
/// @note Uses zlib inflate() for decompression
absl::StatusOr<std::vector<uint8_t>> DecompressZlib(const uint8_t* data,
                                                    size_t compressed_size,
                                                    size_t expected_size);

}  // namespace io
}  // namespace runtime

// Import into fastslide namespace for convenience
using runtime::io::DecompressZlib;
using runtime::io::ReadLeInt32;
using runtime::io::ReadLeUInt32;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_BINARY_UTILS_H_
