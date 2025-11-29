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

#include "simpletiff/tiff_parser.h"

#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "simpletiff/io_utils.h"

namespace simpletiff {

namespace {

/// Header parsing result
struct TiffHeader {
  bool bigtiff = false;
  bool little_endian = true;
  uint64_t first_ifd_offset = 0;
};

/// IFD parsing context - accumulates metadata for a single page
struct IfdContext {
  PageHeader page_header;
  LazyArrayInfo lazy_tile_offsets;
  LazyArrayInfo lazy_tile_bytecounts;
  LazyArrayInfo lazy_strip_offsets;
  LazyArrayInfo lazy_strip_bytecounts;
  uint16_t tile_width = 0;
  uint16_t tile_height = 0;
  uint32_t rows_per_strip = 0;
  uint64_t jpeg_tables_offset = 0;
  uint32_t jpeg_tables_length = 0;
};

// TIFF tag constants
constexpr uint16_t kTagNewSubfileType = 254;
constexpr uint16_t kTagImageWidth = 256;
constexpr uint16_t kTagImageLength = 257;
constexpr uint16_t kTagBitsPerSample = 258;
constexpr uint16_t kTagCompression = 259;
constexpr uint16_t kTagPhotometric = 262;
constexpr uint16_t kTagImageDescription = 270;
constexpr uint16_t kTagStripOffsets = 273;
constexpr uint16_t kTagSamplesPerPixel = 277;
constexpr uint16_t kTagRowsPerStrip = 278;
constexpr uint16_t kTagStripByteCounts = 279;
constexpr uint16_t kTagPredictor = 317;
constexpr uint16_t kTagTileWidth = 322;
constexpr uint16_t kTagTileLength = 323;
constexpr uint16_t kTagTileOffsets = 324;
constexpr uint16_t kTagTileByteCounts = 325;
constexpr uint16_t kTagJpegTables = 347;
constexpr uint16_t kTagXResolution = 282;
constexpr uint16_t kTagYResolution = 283;
constexpr uint16_t kTagResolutionUnit = 296;

// TIFF type sizes
// Types: 0=invalid, 1=BYTE, 2=ASCII, 3=SHORT, 4=LONG, 5=RATIONAL,
//        6=SBYTE, 7=UNDEFINED, 8=SSHORT, 9=SLONG, 10=SRATIONAL,
//        11=FLOAT, 12=DOUBLE, 13=IFD, 14=UNICODE, 15=COMPLEX,
//        16=LONG8 (BigTIFF), 17=SLONG8 (BigTIFF), 18=IFD8 (BigTIFF)
constexpr size_t kTypeSizes[] = {
    0,  // 0: invalid
    1,  // 1: BYTE
    1,  // 2: ASCII
    2,  // 3: SHORT
    4,  // 4: LONG
    8,  // 5: RATIONAL (two LONGs)
    1,  // 6: SBYTE
    1,  // 7: UNDEFINED
    2,  // 8: SSHORT
    4,  // 9: SLONG
    8,  // 10: SRATIONAL (two SLONGs)
    4,  // 11: FLOAT
    8,  // 12: DOUBLE
    4,  // 13: IFD (pointer)
    2,  // 14: UNICODE
    8,  // 15: COMPLEX
    8,  // 16: LONG8 (BigTIFF 64-bit unsigned)
    8,  // 17: SLONG8 (BigTIFF 64-bit signed)
    8   // 18: IFD8 (BigTIFF 64-bit IFD pointer)
};

struct IfdEntry {
  uint16_t tag = 0;
  uint16_t type = 0;
  uint64_t count = 0;
  uint64_t value_offset = 0;
};

// Helper to read different integer types based on endianness
template <typename T>
T ReadInt(const uint8_t* data, bool little_endian) {
  T result = 0;
  if (little_endian) {
    for (size_t i = 0; i < sizeof(T); ++i) {
      result |= static_cast<T>(data[i]) << (i * 8);
    }
  } else {
    for (size_t i = 0; i < sizeof(T); ++i) {
      result |= static_cast<T>(data[i]) << ((sizeof(T) - 1 - i) * 8);
    }
  }
  return result;
}

bool ReadTagData(int fd, size_t file_size, const IfdEntry& entry, bool bigtiff,
                 bool little_endian, std::vector<uint64_t>& out) {
  if (entry.type == 0 ||
      entry.type >= sizeof(kTypeSizes) / sizeof(kTypeSizes[0])) {
    return false;
  }

  const size_t type_size = kTypeSizes[entry.type];
  const size_t total_size = type_size * entry.count;
  const size_t inline_limit = bigtiff ? 8 : 4;

  std::vector<uint8_t> data;
  if (total_size <= inline_limit) {
    // Data is inline in value_offset
    data.resize(total_size);
    if (little_endian) {
      for (size_t i = 0; i < total_size; ++i) {
        data[i] = static_cast<uint8_t>((entry.value_offset >> (i * 8)) & 0xFF);
      }
    } else {
      for (size_t i = 0; i < total_size; ++i) {
        data[i] = static_cast<uint8_t>(
            (entry.value_offset >> ((inline_limit - 1 - i) * 8)) & 0xFF);
      }
    }
  } else {
    // Data is at offset - read using pread
    if (!ReadBytes(fd, file_size, entry.value_offset, total_size, data)) {
      return false;
    }
  }

  out.clear();
  out.reserve(entry.count);

  for (uint64_t i = 0; i < entry.count; ++i) {
    const uint8_t* ptr = data.data() + i * type_size;
    uint64_t value = 0;

    switch (entry.type) {
      case 1:  // BYTE
      case 6:  // SBYTE
        value = ptr[0];
        break;
      case 3:  // SHORT
        value = ReadInt<uint16_t>(ptr, little_endian);
        break;
      case 4:  // LONG
        value = ReadInt<uint32_t>(ptr, little_endian);
        break;
      case 16:  // LONG8 (BigTIFF)
        value = ReadInt<uint64_t>(ptr, little_endian);
        break;
      default:
        value = ReadInt<uint32_t>(ptr, little_endian);
        break;
    }
    out.push_back(value);
  }

  return true;
}

bool ReadTagString(int fd, size_t file_size, const IfdEntry& entry,
                   bool bigtiff, std::string& out) {
  if (entry.type != 2) {  // ASCII type
    return false;
  }

  const size_t total_size = entry.count;
  const size_t inline_limit = bigtiff ? 8 : 4;

  std::vector<uint8_t> data;
  if (total_size <= inline_limit) {
    // Data is inline in value_offset
    data.resize(total_size);
    for (size_t i = 0; i < total_size; ++i) {
      data[i] = static_cast<uint8_t>((entry.value_offset >> (i * 8)) & 0xFF);
    }
  } else {
    // Data is at offset - read using pread
    if (!ReadBytes(fd, file_size, entry.value_offset, total_size, data)) {
      return false;
    }
  }

  // Convert to string, stopping at null terminator
  out.clear();
  for (size_t i = 0; i < data.size() && data[i] != 0; ++i) {
    out.push_back(static_cast<char>(data[i]));
  }

  return true;
}

/// Read a RATIONAL tag value as a double (numerator / denominator)
///
/// @param fd File descriptor
/// @param file_size Total file size
/// @param entry IFD entry for the tag
/// @param bigtiff Whether this is a BigTIFF file
/// @param little_endian Whether file is little-endian
/// @param out Output value (numerator / denominator)
/// @return true on success, false on failure
bool ReadTagRational(int fd, size_t file_size, const IfdEntry& entry,
                     bool bigtiff, bool little_endian, double& out) {
  if (entry.type != 5) {  // RATIONAL type (two LONGs: numerator/denominator)
    return false;
  }

  if (entry.count == 0) {
    return false;
  }

  // RATIONAL is 8 bytes (two 32-bit LONGs)
  // In BigTIFF, 8 bytes fits in value_offset (which is 8 bytes), so it's inline
  // In ClassicTIFF, 8 bytes doesn't fit in value_offset (which is 4 bytes), so
  // it's external
  const size_t total_size = 8;
  const size_t inline_limit = bigtiff ? 8 : 4;
  std::vector<uint8_t> data;

  if (total_size <= inline_limit) {
    // Data is inline in value_offset
    data.resize(total_size);
    if (little_endian) {
      for (size_t i = 0; i < total_size; ++i) {
        data[i] = static_cast<uint8_t>((entry.value_offset >> (i * 8)) & 0xFF);
      }
    } else {
      for (size_t i = 0; i < total_size; ++i) {
        data[i] = static_cast<uint8_t>(
            (entry.value_offset >> ((inline_limit - 1 - i) * 8)) & 0xFF);
      }
    }
  } else {
    // Data is at file offset
    if (!ReadBytes(fd, file_size, entry.value_offset, total_size, data)) {
      return false;
    }
  }

  const uint8_t* ptr = data.data();
  const uint32_t numerator = ReadInt<uint32_t>(ptr, little_endian);
  const uint32_t denominator = ReadInt<uint32_t>(ptr + 4, little_endian);

  if (denominator == 0) {
    return false;  // Avoid division by zero
  }

  out = static_cast<double>(numerator) / static_cast<double>(denominator);
  return true;
}

/// Parse TIFF file header and detect format
///
/// @param fd File descriptor
/// @param file_size Total file size
/// @return TiffHeader on success, empty optional on failure
std::optional<TiffHeader> ParseTiffHeader(int fd, size_t file_size) {
  std::vector<uint8_t> header_data;
  if (!ReadBytes(fd, file_size, 0, 16, header_data)) {
    return std::nullopt;
  }
  const uint8_t* header = header_data.data();

  TiffHeader result;

  // Check byte order (II = little-endian, MM = big-endian)
  if (header[0] == 0x49 && header[1] == 0x49) {
    result.little_endian = true;
  } else if (header[0] == 0x4D && header[1] == 0x4D) {
    result.little_endian = false;
  } else {
    return std::nullopt;  // Invalid byte order marker
  }

  // Check magic number (42 = ClassicTIFF, 43 = BigTIFF)
  const uint16_t magic = ReadInt<uint16_t>(header + 2, result.little_endian);
  if (magic == 42) {
    result.bigtiff = false;
    result.first_ifd_offset =
        ReadInt<uint32_t>(header + 4, result.little_endian);
  } else if (magic == 43) {
    result.bigtiff = true;
    result.first_ifd_offset =
        ReadInt<uint64_t>(header + 8, result.little_endian);
  } else {
    return std::nullopt;  // Invalid magic number
  }

  return result;
}

/// Process a single IFD entry tag and update the context
///
/// @param entry The IFD entry to process
/// @param fd File descriptor
/// @param file_size Total file size
/// @param bigtiff Whether this is a BigTIFF file
/// @param little_endian Whether file is little-endian
/// @param ctx IFD context to update
void ProcessTag(const IfdEntry& entry, int fd, size_t file_size, bool bigtiff,
                bool little_endian, IfdContext& ctx) {
  std::vector<uint64_t> values;

  switch (entry.tag) {
    case kTagImageWidth:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.width = static_cast<uint32_t>(values[0]);
      }
      break;
    case kTagImageLength:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.height = static_cast<uint32_t>(values[0]);
      }
      break;
    case kTagBitsPerSample:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.bits_per_sample = static_cast<uint16_t>(values[0]);
      }
      break;
    case kTagCompression:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.compression = static_cast<uint16_t>(values[0]);
      }
      break;
    case kTagPhotometric:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.photometric = static_cast<uint16_t>(values[0]);
      }
      break;
    case kTagImageDescription:
      ReadTagString(fd, file_size, entry, bigtiff, ctx.page_header.description);
      break;
    case kTagStripOffsets:
      // Defer loading - only store metadata
      ctx.lazy_strip_offsets.tag_type = entry.type;
      ctx.lazy_strip_offsets.count = entry.count;
      ctx.lazy_strip_offsets.value_offset = entry.value_offset;
      break;
    case kTagSamplesPerPixel:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.samples_per_pixel = static_cast<uint16_t>(values[0]);
      }
      break;
    case kTagRowsPerStrip:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.rows_per_strip = static_cast<uint32_t>(values[0]);
      }
      break;
    case kTagStripByteCounts:
      // Defer loading - only store metadata
      ctx.lazy_strip_bytecounts.tag_type = entry.type;
      ctx.lazy_strip_bytecounts.count = entry.count;
      ctx.lazy_strip_bytecounts.value_offset = entry.value_offset;
      break;
    case kTagTileWidth:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.tile_width = static_cast<uint16_t>(values[0]);
      }
      break;
    case kTagTileLength:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.tile_height = static_cast<uint16_t>(values[0]);
      }
      break;
    case kTagTileOffsets:
      // Defer loading - only store metadata
      ctx.lazy_tile_offsets.tag_type = entry.type;
      ctx.lazy_tile_offsets.count = entry.count;
      ctx.lazy_tile_offsets.value_offset = entry.value_offset;
      break;
    case kTagTileByteCounts:
      // Defer loading - only store metadata
      ctx.lazy_tile_bytecounts.tag_type = entry.type;
      ctx.lazy_tile_bytecounts.count = entry.count;
      ctx.lazy_tile_bytecounts.value_offset = entry.value_offset;
      break;
    case kTagJpegTables:
      if (entry.count > 0) {
        ctx.jpeg_tables_offset = entry.value_offset;
        ctx.jpeg_tables_length =
            static_cast<uint32_t>(entry.count * kTypeSizes[entry.type]);
      }
      break;
    case kTagNewSubfileType:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.new_subfile_type = static_cast<uint32_t>(values[0]);
      }
      break;
    case kTagPredictor:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.predictor = static_cast<uint16_t>(values[0]);
      }
      break;
    case kTagXResolution: {
      double resolution;
      if (ReadTagRational(fd, file_size, entry, bigtiff, little_endian,
                          resolution)) {
        ctx.page_header.x_resolution = resolution;
      }
      break;
    }
    case kTagYResolution: {
      double resolution;
      if (ReadTagRational(fd, file_size, entry, bigtiff, little_endian,
                          resolution)) {
        ctx.page_header.y_resolution = resolution;
      }
      break;
    }
    case kTagResolutionUnit:
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.page_header.resolution_unit = static_cast<uint16_t>(values[0]);
      }
      break;
    default:
      // Ignore unknown tags
      break;
  }
}

/// Build page from accumulated IFD context and add to index
///
/// @param ctx IFD context with parsed metadata
/// @param index TIFF index to populate
void BuildPageFromContext(IfdContext& ctx, TiffIndex& index) {
  // Determine storage type and add page
  if (ctx.lazy_tile_offsets.count > 0 && ctx.tile_width > 0 &&
      ctx.tile_height > 0) {
    // Tiled storage
    const uint32_t tiles_x =
        (ctx.page_header.width + ctx.tile_width - 1) / ctx.tile_width;
    const uint32_t tiles_y =
        (ctx.page_header.height + ctx.tile_height - 1) / ctx.tile_height;

    TilesRec tiles;
    tiles.tile_w = ctx.tile_width;
    tiles.tile_h = ctx.tile_height;
    tiles.tiles_x = tiles_x;
    tiles.tiles_y = tiles_y;
    tiles.jpeg_tables_off = ctx.jpeg_tables_offset;
    tiles.jpeg_tables_len = ctx.jpeg_tables_length;
    tiles.lazy_offsets = ctx.lazy_tile_offsets;
    tiles.lazy_bytecounts = ctx.lazy_tile_bytecounts;
    tiles.offsets.count = static_cast<uint32_t>(ctx.lazy_tile_offsets.count);
    tiles.bytecounts.count =
        static_cast<uint32_t>(ctx.lazy_tile_bytecounts.count);

    uint32_t payload_id = index.AddTiles(std::move(tiles));
    ctx.page_header.storage = Storage::kTiles;
    ctx.page_header.payload_id = payload_id;
    index.AddPage(ctx.page_header);

  } else if (ctx.lazy_strip_offsets.count > 0) {
    // Striped storage
    StripsRec strips;
    strips.rows_per_strip = ctx.rows_per_strip;
    strips.jpeg_tables_off = ctx.jpeg_tables_offset;
    strips.jpeg_tables_len = ctx.jpeg_tables_length;
    strips.lazy_offsets = ctx.lazy_strip_offsets;
    strips.lazy_bytecounts = ctx.lazy_strip_bytecounts;
    strips.offsets.count = static_cast<uint32_t>(ctx.lazy_strip_offsets.count);
    strips.bytecounts.count =
        static_cast<uint32_t>(ctx.lazy_strip_bytecounts.count);

    uint32_t payload_id = index.AddStrips(std::move(strips));
    ctx.page_header.storage = Storage::kStrips;
    ctx.page_header.payload_id = payload_id;
    index.AddPage(ctx.page_header);

  } else {
    // Unknown storage - still add to maintain index
    ctx.page_header.storage = Storage::kUnknown;
    index.AddPage(ctx.page_header);
  }
}

}  // namespace

void AddTiledPage(TiffIndex& index, const PageHeader& header,
                  uint16_t tile_width, uint16_t tile_height, uint32_t tiles_x,
                  uint32_t tiles_y, const std::vector<uint64_t>& offsets,
                  const std::vector<uint64_t>& bytecounts,
                  uint64_t jpeg_tables_offset, uint32_t jpeg_tables_length) {
  TilesRec rec;
  rec.tile_w = tile_width;
  rec.tile_h = tile_height;
  rec.tiles_x = tiles_x;
  rec.tiles_y = tiles_y;

  // If offsets/bytecounts are empty, they'll be lazy loaded
  if (!offsets.empty()) {
    rec.offsets = index.AppendOffsets(offsets);
    rec.lazy_offsets.loaded = true;
  }

  if (!bytecounts.empty()) {
    rec.bytecounts = index.AppendBytecounts(bytecounts);
    rec.lazy_bytecounts.loaded = true;
  }

  rec.jpeg_tables_off = jpeg_tables_offset;
  rec.jpeg_tables_len = jpeg_tables_length;

  uint32_t payload_id = index.AddTiles(std::move(rec));

  PageHeader h = header;
  h.storage = Storage::kTiles;
  h.payload_id = payload_id;
  index.AddPage(h);
}

void AddStripedPage(TiffIndex& index, const PageHeader& header,
                    uint32_t rows_per_strip,
                    const std::vector<uint64_t>& offsets,
                    const std::vector<uint64_t>& bytecounts,
                    uint64_t jpeg_tables_offset, uint32_t jpeg_tables_length) {
  StripsRec rec;
  rec.rows_per_strip = rows_per_strip;

  // If offsets/bytecounts are empty, they'll be lazy loaded
  if (!offsets.empty()) {
    rec.offsets = index.AppendOffsets(offsets);
    rec.lazy_offsets.loaded = true;
  }

  if (!bytecounts.empty()) {
    rec.bytecounts = index.AppendBytecounts(bytecounts);
    rec.lazy_bytecounts.loaded = true;
  }

  rec.jpeg_tables_off = jpeg_tables_offset;
  rec.jpeg_tables_len = jpeg_tables_length;

  uint32_t payload_id = index.AddStrips(std::move(rec));

  PageHeader h = header;
  h.storage = Storage::kStrips;
  h.payload_id = payload_id;
  index.AddPage(h);
}

void AddSingleJpegPage(TiffIndex& index, const PageHeader& header,
                       uint64_t offset, uint64_t length) {
  uint32_t payload_id = index.AddSingleJpeg(SingleJpegRec{offset, length});

  PageHeader h = header;
  h.storage = Storage::kSingleJpeg;
  h.payload_id = payload_id;
  index.AddPage(h);
}

bool ParseTiff(int fd, size_t file_size, TiffIndex& index) {
  // Parse TIFF header
  auto header_opt = ParseTiffHeader(fd, file_size);
  if (!header_opt) {
    return false;  // Invalid TIFF header
  }
  const TiffHeader& tiff_header = *header_opt;

  // Set format information in index
  index.SetFormat(tiff_header.bigtiff, tiff_header.little_endian, file_size);

  // Iterate through IFD chain
  uint64_t ifd_offset = tiff_header.first_ifd_offset;
  const bool bigtiff = tiff_header.bigtiff;
  const bool little_endian = tiff_header.little_endian;

  // Temporary buffer for reading IFD data
  std::vector<uint8_t> ifd_buffer;

  // Parse all IFDs
  uint32_t ifd_index = 0;
  while (ifd_offset != 0) {
    // Read number of entries
    const size_t count_size = bigtiff ? 8 : 2;
    std::vector<uint8_t> count_data;
    if (!ReadBytes(fd, file_size, ifd_offset, count_size, count_data)) {
      break;
    }

    const uint64_t num_entries =
        bigtiff ? ReadInt<uint64_t>(count_data.data(), little_endian)
                : ReadInt<uint16_t>(count_data.data(), little_endian);

    // Initialize IFD context for this page
    IfdContext ctx;
    ctx.page_header.ifd_index = ifd_index;
    ctx.page_header.ifd_offset = ifd_offset;

    const size_t entry_size = bigtiff ? 20 : 12;
    const size_t offset_size = bigtiff ? 8 : 4;
    const uint64_t entries_start_offset = ifd_offset + count_size;

    // Batch read all IFD entries + next offset
    const size_t total_entries_size = num_entries * entry_size;
    const size_t ifd_data_size = total_entries_size + offset_size;
    if (!ReadBytes(fd, file_size, entries_start_offset, ifd_data_size,
                   ifd_buffer)) {
      break;
    }

    // Process each IFD entry
    for (uint64_t i = 0; i < num_entries; ++i) {
      const uint8_t* entry_data = ifd_buffer.data() + (i * entry_size);

      // Parse entry from buffer
      IfdEntry entry;
      entry.tag = ReadInt<uint16_t>(entry_data, little_endian);
      entry.type = ReadInt<uint16_t>(entry_data + 2, little_endian);

      if (bigtiff) {
        entry.count = ReadInt<uint64_t>(entry_data + 4, little_endian);
        entry.value_offset = ReadInt<uint64_t>(entry_data + 12, little_endian);
      } else {
        entry.count = ReadInt<uint32_t>(entry_data + 4, little_endian);
        entry.value_offset = ReadInt<uint32_t>(entry_data + 8, little_endian);
      }

      // Process tag and update context
      ProcessTag(entry, fd, file_size, bigtiff, little_endian, ctx);
    }

    // Build page from accumulated context and add to index
    BuildPageFromContext(ctx, index);

    // Parse next IFD offset from the buffer
    const uint8_t* next_offset_data = ifd_buffer.data() + total_entries_size;
    ifd_offset = bigtiff ? ReadInt<uint64_t>(next_offset_data, little_endian)
                         : ReadInt<uint32_t>(next_offset_data, little_endian);

    ++ifd_index;
  }

  return index.NumPages() > 0;
}

bool OpenTiff(std::string_view filepath, TiffIndex& index, int& out_fd) {
  const int fd = aifocore::portable_open(std::string(filepath).c_str(),
                                         O_RDONLY | O_BINARY);
  if (fd < 0) {
    return false;
  }

  portable_stat_struct st{};

  if (aifocore::portable_fstat(fd, &st) != 0) {
    aifocore::portable_close(fd);
    return false;
  }

  const size_t file_size = static_cast<size_t>(st.st_size);

  // Parse the TIFF file using pread
  if (!ParseTiff(fd, file_size, index)) {
    aifocore::portable_close(fd);
    return false;
  }

  // Store file descriptor in index for future access
  index.SetFd(fd);

  out_fd = fd;
  return true;
}

// Helper to read a single element from a tag array
static bool ReadSingleTagElement(int fd, size_t file_size,
                                 const LazyArrayInfo& lazy_info, bool bigtiff,
                                 bool little_endian, uint64_t element_index,
                                 uint64_t& out_value) {
  if (element_index >= lazy_info.count) {
    return false;
  }

  const uint16_t type = lazy_info.tag_type;
  if (type == 0 || type >= sizeof(kTypeSizes) / sizeof(kTypeSizes[0])) {
    return false;
  }

  const size_t type_size = kTypeSizes[type];
  const size_t inline_limit = bigtiff ? 8 : 4;
  const size_t total_size = type_size * lazy_info.count;

  // Calculate file offset for this specific element
  uint64_t file_offset;
  if (total_size <= inline_limit) {
    // Data is inline - extract the specific element from value_offset
    const size_t byte_offset = element_index * type_size;
    uint64_t inline_data = lazy_info.value_offset;
    if (little_endian) {
      inline_data >>= (byte_offset * 8);
    } else {
      inline_data >>= ((inline_limit - byte_offset - type_size) * 8);
    }

    // Mask to type size
    switch (type) {
      case 1:  // BYTE
      case 6:  // SBYTE
        out_value = inline_data & 0xFF;
        break;
      case 3:  // SHORT
        out_value = inline_data & 0xFFFF;
        break;
      case 4:  // LONG
        out_value = inline_data & 0xFFFFFFFF;
        break;
      default:
        out_value = inline_data & 0xFFFFFFFF;
        break;
    }
    return true;
  } else {
    // Data is at file offset - read just this element
    file_offset = lazy_info.value_offset + (element_index * type_size);
  }

  // Read the single element from file using pread
  std::vector<uint8_t> data;
  if (!ReadBytes(fd, file_size, file_offset, type_size, data)) {
    return false;
  }

  const uint8_t* ptr = data.data();
  switch (type) {
    case 1:  // BYTE
    case 6:  // SBYTE
      out_value = ptr[0];
      break;
    case 3:  // SHORT
      out_value = ReadInt<uint16_t>(ptr, little_endian);
      break;
    case 4:  // LONG
      out_value = ReadInt<uint32_t>(ptr, little_endian);
      break;
    case 16:  // LONG8 (BigTIFF)
      out_value = ReadInt<uint64_t>(ptr, little_endian);
      break;
    default:
      out_value = ReadInt<uint32_t>(ptr, little_endian);
      break;
  }

  return true;
}

bool EnsureTileLoaded(const TiffIndex& index, uint32_t page_index,
                      uint32_t tile_or_strip_index, uint64_t& out_offset,
                      uint64_t& out_bytecount) {
  if (page_index >= index.NumPages()) {
    return false;
  }

  const auto& page = index.Page(page_index);

  // Lock for thread-safe access
  std::lock_guard<std::mutex> lock(index.LazyMutex());

  if (page.storage == Storage::kTiles) {
    const auto& tiles_rec = index.Tiles(page.payload_id);

    // If already loaded, use cached data
    if (tiles_rec.lazy_offsets.loaded) {
      auto offsets = index.Offsets(tiles_rec.offsets);
      auto bytecounts = index.Bytecounts(tiles_rec.bytecounts);
      if (tile_or_strip_index >= offsets.size()) {
        return false;
      }
      out_offset = offsets[tile_or_strip_index];
      out_bytecount = bytecounts[tile_or_strip_index];
      return true;
    }

    // Load just this single tile's metadata
    if (!ReadSingleTagElement(index.Fd(), index.FileSize(),
                              tiles_rec.lazy_offsets, index.IsBigTiff(),
                              index.IsLittleEndian(), tile_or_strip_index,
                              out_offset)) {
      return false;
    }
    if (!ReadSingleTagElement(index.Fd(), index.FileSize(),
                              tiles_rec.lazy_bytecounts, index.IsBigTiff(),
                              index.IsLittleEndian(), tile_or_strip_index,
                              out_bytecount)) {
      return false;
    }
    return true;

  } else if (page.storage == Storage::kStrips) {
    const auto& strips_rec = index.Strips(page.payload_id);

    // If already loaded, use cached data
    if (strips_rec.lazy_offsets.loaded) {
      auto offsets = index.Offsets(strips_rec.offsets);
      auto bytecounts = index.Bytecounts(strips_rec.bytecounts);
      if (tile_or_strip_index >= offsets.size()) {
        return false;
      }
      out_offset = offsets[tile_or_strip_index];
      out_bytecount = bytecounts[tile_or_strip_index];
      return true;
    }

    // Load just this single strip's metadata
    if (!ReadSingleTagElement(index.Fd(), index.FileSize(),
                              strips_rec.lazy_offsets, index.IsBigTiff(),
                              index.IsLittleEndian(), tile_or_strip_index,
                              out_offset)) {
      return false;
    }
    if (!ReadSingleTagElement(index.Fd(), index.FileSize(),
                              strips_rec.lazy_bytecounts, index.IsBigTiff(),
                              index.IsLittleEndian(), tile_or_strip_index,
                              out_bytecount)) {
      return false;
    }
    return true;
  }

  return false;
}

bool EnsurePageLoaded(const TiffIndex& index, uint32_t page_index) {
  if (page_index >= index.NumPages()) {
    return false;
  }

  const auto& page = index.Page(page_index);

  // Lock for thread-safe lazy loading
  std::lock_guard<std::mutex> lock(index.LazyMutex());

  if (page.storage == Storage::kTiles) {
    auto& tiles_rec = index.MutableTilesPool()[page.payload_id];

    // Load offsets if not yet loaded
    if (!tiles_rec.lazy_offsets.loaded && tiles_rec.lazy_offsets.count > 0) {
      IfdEntry entry;
      entry.type = tiles_rec.lazy_offsets.tag_type;
      entry.count = tiles_rec.lazy_offsets.count;
      entry.value_offset = tiles_rec.lazy_offsets.value_offset;

      std::vector<uint64_t> offsets;
      if (!ReadTagData(index.Fd(), index.FileSize(), entry, index.IsBigTiff(),
                       index.IsLittleEndian(), offsets)) {
        return false;
      }

      // Store in arena
      tiles_rec.offsets.start =
          static_cast<uint32_t>(index.MutableOffsetsArena().size());
      tiles_rec.offsets.count = static_cast<uint32_t>(offsets.size());
      index.MutableOffsetsArena().insert(index.MutableOffsetsArena().end(),
                                         offsets.begin(), offsets.end());
      tiles_rec.lazy_offsets.loaded = true;
    }

    // Load bytecounts if not yet loaded
    if (!tiles_rec.lazy_bytecounts.loaded &&
        tiles_rec.lazy_bytecounts.count > 0) {
      IfdEntry entry;
      entry.type = tiles_rec.lazy_bytecounts.tag_type;
      entry.count = tiles_rec.lazy_bytecounts.count;
      entry.value_offset = tiles_rec.lazy_bytecounts.value_offset;

      std::vector<uint64_t> bytecounts;
      if (!ReadTagData(index.Fd(), index.FileSize(), entry, index.IsBigTiff(),
                       index.IsLittleEndian(), bytecounts)) {
        return false;
      }

      // Store in arena
      tiles_rec.bytecounts.start =
          static_cast<uint32_t>(index.MutableBytecountsArena().size());
      tiles_rec.bytecounts.count = static_cast<uint32_t>(bytecounts.size());
      index.MutableBytecountsArena().insert(
          index.MutableBytecountsArena().end(), bytecounts.begin(),
          bytecounts.end());
      tiles_rec.lazy_bytecounts.loaded = true;
    }

  } else if (page.storage == Storage::kStrips) {
    auto& strips_rec = index.MutableStripsPool()[page.payload_id];

    // Load offsets if not yet loaded
    if (!strips_rec.lazy_offsets.loaded && strips_rec.lazy_offsets.count > 0) {
      IfdEntry entry;
      entry.type = strips_rec.lazy_offsets.tag_type;
      entry.count = strips_rec.lazy_offsets.count;
      entry.value_offset = strips_rec.lazy_offsets.value_offset;

      std::vector<uint64_t> offsets;
      if (!ReadTagData(index.Fd(), index.FileSize(), entry, index.IsBigTiff(),
                       index.IsLittleEndian(), offsets)) {
        return false;
      }

      // Store in arena
      strips_rec.offsets.start =
          static_cast<uint32_t>(index.MutableOffsetsArena().size());
      strips_rec.offsets.count = static_cast<uint32_t>(offsets.size());
      index.MutableOffsetsArena().insert(index.MutableOffsetsArena().end(),
                                         offsets.begin(), offsets.end());
      strips_rec.lazy_offsets.loaded = true;
    }

    // Load bytecounts if not yet loaded
    if (!strips_rec.lazy_bytecounts.loaded &&
        strips_rec.lazy_bytecounts.count > 0) {
      IfdEntry entry;
      entry.type = strips_rec.lazy_bytecounts.tag_type;
      entry.count = strips_rec.lazy_bytecounts.count;
      entry.value_offset = strips_rec.lazy_bytecounts.value_offset;

      std::vector<uint64_t> bytecounts;
      if (!ReadTagData(index.Fd(), index.FileSize(), entry, index.IsBigTiff(),
                       index.IsLittleEndian(), bytecounts)) {
        return false;
      }

      // Store in arena
      strips_rec.bytecounts.start =
          static_cast<uint32_t>(index.MutableBytecountsArena().size());
      strips_rec.bytecounts.count = static_cast<uint32_t>(bytecounts.size());
      index.MutableBytecountsArena().insert(
          index.MutableBytecountsArena().end(), bytecounts.begin(),
          bytecounts.end());
      strips_rec.lazy_bytecounts.loaded = true;
    }
  }

  return true;
}

}  // namespace simpletiff
