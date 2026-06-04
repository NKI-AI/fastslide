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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_PARSE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_PARSE_H_

/// @file czi_parse.h
/// @brief Pure, side-effect-free decoders for ZISRAW directory records.
///
/// These helpers operate on raw little-endian byte spans and contain no I/O,
/// which keeps them independently unit-testable. The byte offsets and field
/// widths below are taken directly from the public Zeiss CZI / ZISRAW binary
/// format definition; reserved/spare regions are intentionally treated as
/// opaque rather than re-interpreted.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/czi/czi_level_info.h"

namespace fastslide {
namespace czi {

/// Byte length of the fixed prefix of a `DirectoryEntryDV` record:
/// schema[2] + pixel_type[4] + file_position[8] + file_part[4] +
/// compression[4] + reserved[6] + dimension_count[4] = 32 bytes.
inline constexpr size_t kDirEntryFixedSize = 32;

/// Byte length of one `DimensionEntryDV` record:
/// dimension[4] + start[4] + size[4] + start_coordinate[4] + stored_size[4].
inline constexpr size_t kDimensionEntrySize = 20;

/// Byte length of the fixed 32-byte segment header (`SID` + allocated_size +
/// used_size) shared by every ZISRAW segment.
inline constexpr size_t kSegmentHeaderLen = 32;

/// Minimum byte length of a ZISRAWSUBBLOCK segment's data section before the
/// metadata + pixel payload, per the ZISRAW layout (`SIZE_SUBBLOCKDATA_MINIMUM`
/// in the public format definition).
inline constexpr size_t kSubblockDataMinLen = 256;

/// @brief Fixed-size header of a `DirectoryEntryDV` directory record.
struct DirEntryHeader {
  int32_t pixel_type = 0;       ///< Pixel-type code.
  int64_t file_position = 0;    ///< Offset of the subblock segment.
  int32_t compression = 0;      ///< Compression code.
  int32_t dimension_count = 0;  ///< Number of trailing dimension records.
};

/// @brief One decoded `DimensionEntryDV` record.
struct DimensionRecord {
  char axis = '\0';         ///< Dimension label (e.g. 'X', 'Y', 'S').
  int32_t start = 0;        ///< Logical start coordinate.
  int32_t size = 0;         ///< Logical (full-resolution) size.
  int32_t stored_size = 0;  ///< Physically stored size on disk.
};

/// @brief Decode the 32-byte fixed prefix of a `DirectoryEntryDV`.
///
/// Validates the two-byte schema tag is "DV". The six reserved bytes after
/// the compression field are skipped without interpretation.
///
/// @param bytes At least `kDirEntryFixedSize` bytes of directory data.
/// @return Decoded header, or an InvalidArgument status on a short buffer or
///   bad schema tag.
[[nodiscard]] aifocore::Result<DirEntryHeader> ParseDirEntryHeader(
    std::span<const uint8_t> bytes);

/// @brief Decode one 20-byte `DimensionEntryDV` record.
///
/// The float `start_coordinate` field is not needed for tiling and is
/// skipped. `axis` is the first byte of the four-byte dimension label.
///
/// @param bytes At least `kDimensionEntrySize` bytes.
/// @return Decoded record, or an InvalidArgument status on a short buffer.
[[nodiscard]] aifocore::Result<DimensionRecord> ParseDimensionRecord(
    std::span<const uint8_t> bytes);

/// @brief Round `full_size / stored_size` to the nearest positive integer.
///
/// A CZI pyramid subblock encodes its level implicitly via the ratio of its
/// logical (full-resolution) size to its physically stored size. This mirrors
/// the BSD-licensed `czifile` scale-factor convention (`shape / stored_shape`,
/// rounded; see `CziImage._scale_factors`), reduced here to the single integer
/// downsample FastSlide uses as a pyramid-level key. The result is clamped to
/// `[1, INT32_MAX]`.
[[nodiscard]] int32_t DownsampleFromSizes(int32_t full_size,
                                          int32_t stored_size);

/// @brief Byte length of the fixed portion of a ZISRAWSUBBLOCK segment that
/// precedes the metadata + pixel payload.
///
/// Computed the way `czifile` does it (`CziSubBlockSegment.__init__`): the
/// 32-byte segment header, followed by `max(256, 16 + inline_directory_entry)`
/// bytes, where the inline `DirectoryEntryDV` occupies
/// `kDirEntryFixedSize + dimension_count * kDimensionEntrySize` bytes. For the
/// common case of <= 10 dimensions this evaluates to 288, but unlike a
/// hardcoded constant it stays correct for high-dimensional entries.
///
/// @param dimension_count Number of `DimensionEntryDV` records in the entry.
[[nodiscard]] size_t SubblockFixedHeaderLength(int32_t dimension_count);

/// @brief Decompress a zstd frame into a buffer of exactly `expected_size`.
///
/// Returns an error if the zstd stream is malformed or its decompressed length
/// does not match `expected_size`.
[[nodiscard]] aifocore::Result<std::vector<uint8_t>> DecompressZstd(
    std::span<const uint8_t> in, size_t expected_size);

/// @brief Result of parsing the small container header in a zstd1 payload.
struct Zstd1Payload {
  std::span<const uint8_t> payload;  ///< The zstd frame, header stripped.
  bool do_hilo = false;              ///< Whether 16-bit HiLo unpacking applies.
};

/// @brief Strip the zstd1 container header and report the HiLo flag.
///
/// The zstd1 payload begins with a one-byte header length. Known values are 1
/// (length byte only) and 3 (length byte + chunk type + flags); chunk type
/// must be 1 and the HiLo flag is the LSB of the flags byte. This header layout
/// is defined in the public format and is present in `czifile`
/// (`CziSubBlock.data`, `compression == 6`).
[[nodiscard]] aifocore::Result<Zstd1Payload> ParseZstd1Payload(
    std::span<const uint8_t> in);

/// @brief Undo the zstd1 16-bit "HiLo" byte shuffle.
///
/// The low bytes of every 16-bit sample are stored in the first half of the
/// buffer and the high bytes in the second half; this interleaves them back
/// into native little-endian 16-bit order. Requires an even byte count.
[[nodiscard]] aifocore::Result<std::vector<uint8_t>> UnpackHiLo16(
    std::span<const uint8_t> in);

/// @brief A set of subblock indices that share one scene id.
struct SceneGroup {
  int32_t scene_id = 0;                    ///< CZI scene ("S") index.
  std::vector<uint32_t> subblock_indices;  ///< Indices into the subblock array.
};

/// @brief Partition subblocks by their scene id.
///
/// Returns one group per distinct `scene` value, ordered by ascending scene
/// id. Within each group, subblock indices preserve their original order.
[[nodiscard]] std::vector<SceneGroup> GroupSubblocksByScene(
    std::span<const CziSubblockInfo> subblocks);

/// @brief Sorted, de-duplicated copy of `values`.
///
/// Used to enumerate the distinct CZI `Z` (focal) or `T` (time) dimension
/// starts present in a scene, so a zero-based plane selector can be mapped to
/// the absolute dimension start it addresses. Mirrors how `czifile` derives a
/// dimension's `sizes`/coordinate set from the per-subblock dimension records.
[[nodiscard]] std::vector<int32_t> SortedUniqueAxis(
    std::span<const int32_t> values);

}  // namespace czi
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_PARSE_H_
