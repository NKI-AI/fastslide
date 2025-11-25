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
//
// TIFF parser for building the index structure

#ifndef SIMPLETIFF_TIFF_PARSER_H_
#define SIMPLETIFF_TIFF_PARSER_H_

#include <cstdint>
#include <string_view>

#include "simpletiff/index.h"

namespace simpletiff {

/// Parse a TIFF file and build an index
///
/// Reads all IFDs in the TIFF file and constructs an efficient
/// index structure for fast random access.
///
/// @param fd File descriptor (must be open for reading)
/// @param file_size Size of the file in bytes
/// @param index Output index structure
/// @return true on success, false on failure
bool ParseTiff(int fd, uint64_t file_size, TiffIndex& index);

/// Open a TIFF file and build an index
///
/// Convenience function that opens the file, parses it, and returns
/// the index.
///
/// @param filepath Path to TIFF file
/// @param index Output index structure
/// @param out_fd Output file descriptor (caller must close)
/// @return true on success, false on failure
bool OpenTiff(std::string_view filepath, TiffIndex& index, int& out_fd);

/// Ensure page data is loaded (lazy load if needed)
///
/// Thread-safe function that ensures offset/bytecount arrays are loaded
/// for the given page. This is called automatically by reader functions.
///
/// @param index TIFF index (mutable fields updated if lazy loading)
/// @param page_index Page index to ensure is loaded
/// @return true on success, false on failure
bool EnsurePageLoaded(const TiffIndex& index, uint32_t page_index);

/// Ensure single tile/strip metadata is loaded (lazy load if needed)
///
/// Optimized for single-tile access pattern. Only loads the specific
/// offset/bytecount pair needed, not the entire arrays.
///
/// @param index TIFF index (mutable fields updated if lazy loading)
/// @param page_index Page index
/// @param tile_or_strip_index Tile or strip index within the page
/// @param out_offset Output offset in file
/// @param out_bytecount Output bytecount
/// @return true on success, false on failure
bool EnsureTileLoaded(const TiffIndex& index, uint32_t page_index,
                      uint32_t tile_or_strip_index, uint64_t& out_offset,
                      uint64_t& out_bytecount);

/// Add a tiled page to the index
///
/// @param index Index to update
/// @param header Page header
/// @param tile_width Tile width in pixels
/// @param tile_height Tile height in pixels
/// @param tiles_x Number of tiles horizontally
/// @param tiles_y Number of tiles vertically
/// @param offsets Tile offsets in file
/// @param bytecounts Tile byte counts
/// @param jpeg_tables_offset JPEG tables offset (0 if none)
/// @param jpeg_tables_length JPEG tables length (0 if none)
void AddTiledPage(TiffIndex& index, const PageHeader& header,
                  uint16_t tile_width, uint16_t tile_height, uint32_t tiles_x,
                  uint32_t tiles_y, const std::vector<uint64_t>& offsets,
                  const std::vector<uint64_t>& bytecounts,
                  uint64_t jpeg_tables_offset, uint32_t jpeg_tables_length);

/// Add a strip-based page to the index
///
/// @param index Index to update
/// @param header Page header
/// @param rows_per_strip Rows per strip
/// @param offsets Strip offsets in file
/// @param bytecounts Strip byte counts
/// @param jpeg_tables_offset JPEG tables offset (0 if none)
/// @param jpeg_tables_length JPEG tables length (0 if none)
void AddStripedPage(TiffIndex& index, const PageHeader& header,
                    uint32_t rows_per_strip,
                    const std::vector<uint64_t>& offsets,
                    const std::vector<uint64_t>& bytecounts,
                    uint64_t jpeg_tables_offset, uint32_t jpeg_tables_length);

/// Add a single JPEG page to the index
///
/// @param index Index to update
/// @param header Page header
/// @param offset JPEG data offset in file
/// @param length JPEG data length in bytes
void AddSingleJpegPage(TiffIndex& index, const PageHeader& header,
                       uint64_t offset, uint64_t length);

}  // namespace simpletiff

#endif  // SIMPLETIFF_TIFF_PARSER_H_
