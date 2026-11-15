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
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/utilities/hash.h"
#include "simpletiff/index.h"

namespace fastslide::readers::tiff_quickhash {

/// @brief Names the TIFF pages a reader wants folded into its quickhash.
///
/// This is the only thing a TIFF-based reader has to supply;
/// `TiffBasedReader::GetQuickHash()` does the rest, so no reader needs its own
/// copy of the hashing recipe.
struct Spec {
  /// @brief Parsed index for the file. Must outlive the call.
  const simpletiff::TiffIndex* index = nullptr;

  /// @brief IFDs of the lowest-resolution level, whose raw compressed bytes
  ///        identify the slide.
  ///
  /// Single-plane formats list one page. Channel-per-page formats (QPTIFF,
  /// OME-TIFF) list every page of that level, in the order the reader presents
  /// them; the order is part of the digest.
  std::vector<uint32_t> level_pages;

  /// @brief IFD the `tiff.*` property strings are read from.
  ///
  /// This is OpenSlide's `property_dir`, which is 0 for every format it
  /// supports except Leica SCN.
  uint32_t property_page = 0;
};

/// @brief Compute the quickhash described by @p spec.
///
/// Hashes the raw compressed bytes of every page in `spec.level_pages`, then
/// the nine `tiff.*` property strings from `spec.property_page`, in OpenSlide's
/// order.
///
/// @note OpenSlide gives up on levels over 5 MiB to keep `openslide_open()`
///       fast. fastslide computes the digest lazily on `.quickhash` access
///       rather than at open time, so that ceiling buys nothing here and is
///       deliberately absent: every slide gets a digest.
///
/// @param spec Pages to digest.
/// @return Lowercase 64-character hex digest, never empty.
/// @retval kInvalidArgument if the spec names no index or no pages.
[[nodiscard]] aifocore::Result<std::string> Compute(const Spec& spec);

/// @brief Hash the raw compressed bytes of one TIFF page (tiles or strips).
///
/// Exposed for tests and for readers whose recipe is not just "the smallest
/// level"; prefer Compute().
///
/// @param tiff_index Parsed TIFF index.
/// @param page TIFF page (IFD) to hash.
/// @param hasher Hash accumulator.
/// @return OkStatus() on success.
/// @retval kOutOfRange if @p page is not in @p tiff_index.
/// @retval kInternal if the page is neither tiled nor stripped, or holds no
///         data; a pyramid level in that state means a malformed file.
aifocore::Status HashPageRawCompressedBytes(
    const simpletiff::TiffIndex& tiff_index, uint32_t page,
    QuickHashBuilder& hasher);

/// @brief Hash the nine `tiff.*` property strings OpenSlide folds into
///        quickhash-1, as NUL-terminated name + value pairs.
///
/// Mirrors OpenSlide's `store_and_hash_properties()`, including its treatment
/// of an absent tag as an empty value (the name is still hashed).
///
/// @param tiff_index Parsed TIFF index.
/// @param property_page IFD to read the tags from.
/// @param hasher Hash accumulator.
aifocore::Status HashTiffProperties(const simpletiff::TiffIndex& tiff_index,
                                    uint32_t property_page,
                                    QuickHashBuilder& hasher);

}  // namespace fastslide::readers::tiff_quickhash

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_QUICKHASH_H_
