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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_QUICKHASH_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_QUICKHASH_H_

#include <cstdint>
#include <filesystem>

#include "aifocore/status/result.h"
#include "fastslide/utilities/hash.h"
#include "simpletiff/index.h"

namespace fastslide::readers::tiff_quickhash {

/// @brief Hash raw compressed bytes from a single TIFF page for
/// OpenSlide-compatible quickhash.
///
/// This hashes the raw compressed bytes of the lowest-resolution level (tiles
/// or strips), matching OpenSlide's behavior. To keep open() fast, OpenSlide
/// disables quickhash when the total compressed size exceeds ~5MiB.
///
/// @param tiff_index Parsed TIFF index.
/// @param page TIFF page (IFD) to hash.
/// @param filename File path used for HashFilePart().
/// @param hasher Hash accumulator.
/// @param max_total_compressed_bytes Maximum total compressed bytes to hash
/// before returning
///        OutOfRange.
/// @return OkStatus() on success; OutOfRange if too large; or another Status on
/// error.
aifocore::Status HashPageRawCompressedBytes(
    const simpletiff::TiffIndex& tiff_index, uint16_t page,
    const std::filesystem::path& filename, QuickHashBuilder& hasher,
    int64_t max_total_compressed_bytes = (5LL << 20));

/// @brief Hash selected TIFF properties as NUL-terminated name + value strings.
///
/// This matches OpenSlide's property hashing order (store_and_hash_properties)
/// used for tifflike quickhash generation.
///
/// Currently, `simpletiff` exposes:
/// - ImageDescription via PageHeader::description
/// - Software via PageHeader::software
///
/// Missing properties are hashed as empty strings.
///
/// @param tiff_index Parsed TIFF index.
/// @param hasher Hash accumulator.
void HashTiffProperties(const simpletiff::TiffIndex& tiff_index,
                        QuickHashBuilder& hasher);

}  // namespace fastslide::readers::tiff_quickhash

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_QUICKHASH_H_
