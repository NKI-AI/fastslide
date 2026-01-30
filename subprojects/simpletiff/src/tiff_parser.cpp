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

#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "simpletiff/internal/ndpi_mcu_tiling.h"
#include "simpletiff/io_utils.h"

namespace simpletiff {

namespace {

/// Header parsing result
struct TiffHeader {
  bool bigtiff = false;
  bool little_endian = true;
  bool ndpi_classictiff_64bit = false;
  uint64_t first_ifd_offset = 0;
};

/// IFD parsing context - accumulates metadata for a single page
struct IfdContext {
  PageHeader page_header;
  LazyArrayInfo lazy_tile_offsets;
  LazyArrayInfo lazy_tile_bytecounts;
  LazyArrayInfo lazy_strip_offsets;
  LazyArrayInfo lazy_strip_bytecounts;
  LazyArrayInfo lazy_offset_high_bytes;
  LazyArrayInfo lazy_ndpi_mcu_starts;
  uint16_t tile_width = 0;
  uint16_t tile_height = 0;
  uint32_t rows_per_strip = 0;
  uint64_t jpeg_tables_offset = 0;
  uint32_t jpeg_tables_length = 0;
  std::vector<uint64_t> sub_ifd_offsets;
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
constexpr uint16_t kTagSoftware = 305;
constexpr uint16_t kTagSubIFDs = 330;
constexpr uint16_t kTagOffsetHighBytes =
    65324;                                      // NDPI: high 32 bits of offsets
constexpr uint16_t kTagNdpiSourceLens = 65421;  // NDPI: SourceLens
constexpr uint16_t kTagNdpiMetadata = 65449;    // NDPI: metadata blob
constexpr uint16_t kTagNdpiMcuStarts =
    65426;  // NDPI: MCU starts (tile starts within strip)

constexpr uint16_t kCompressionJpeg = 7;

// TIFF type sizes
// Types: 0=invalid, 1=BYTE, 2=ASCII, 3=SHORT, 4=LONG, 5=RATIONAL,
//        6=SBYTE, 7=UNDEFINED, 8=SSHORT, 9=SLONG, 10=SRATIONAL,
//        11=FLOAT, 12=DOUBLE, 13=IFD, 14=UNICODE, 15=COMPLEX,
//        16=LONG8 (BigTIFF), 17=SLONG8 (BigTIFF), 18=IFD8 (BigTIFF)
constexpr std::array<size_t, 19> kTypeSizes = {
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
  if (entry.type == 0 || entry.type >= kTypeSizes.size()) {
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
      case 4:   // LONG
      case 13:  // IFD (pointer, ClassicTIFF)
        value = ReadInt<uint32_t>(ptr, little_endian);
        break;
      case 16:  // LONG8 (BigTIFF)
      case 18:  // IFD8 (BigTIFF)
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

/// Read a numeric tag as double (first element only), interpreting integer
/// types as signed. Intended for vendor tags like NDPI SourceLens where values
/// may be stored as unsigned but represent signed sentinel values (-1, -2).
bool ReadTagSignedDouble(int fd, size_t file_size, const IfdEntry& entry,
                         bool bigtiff, bool little_endian, double& out) {
  if (entry.count == 0) {
    return false;
  }
  if (entry.type == 0 || entry.type >= kTypeSizes.size()) {
    return false;
  }

  const size_t type_size = kTypeSizes[entry.type];
  if (type_size == 0) {
    return false;
  }
  const size_t total_size = type_size * entry.count;
  const size_t inline_limit = bigtiff ? 8 : 4;

  std::vector<uint8_t> data;
  if (total_size <= inline_limit) {
    data.resize(type_size);
    if (little_endian) {
      for (size_t i = 0; i < type_size; ++i) {
        data[i] = static_cast<uint8_t>((entry.value_offset >> (i * 8)) & 0xFF);
      }
    } else {
      for (size_t i = 0; i < type_size; ++i) {
        data[i] = static_cast<uint8_t>(
            (entry.value_offset >> ((inline_limit - 1 - i) * 8)) & 0xFF);
      }
    }
  } else {
    if (!ReadBytes(fd, file_size, entry.value_offset, type_size, data)) {
      return false;
    }
  }

  const uint8_t* ptr = data.data();
  switch (entry.type) {
    case 1:  // BYTE
    case 6:  // SBYTE
      out = static_cast<double>(static_cast<int8_t>(ptr[0]));
      return true;
    case 3:  // SHORT
    case 8:  // SSHORT
      out = static_cast<double>(ReadInt<int16_t>(ptr, little_endian));
      return true;
    case 4:   // LONG
    case 9:   // SLONG
    case 13:  // IFD
      out = static_cast<double>(ReadInt<int32_t>(ptr, little_endian));
      return true;
    case 16:  // LONG8
    case 17:  // SLONG8
    case 18:  // IFD8
      out = static_cast<double>(ReadInt<int64_t>(ptr, little_endian));
      return true;
    case 11: {  // FLOAT
      const uint32_t bits = ReadInt<uint32_t>(ptr, little_endian);
      float f = 0.0F;
      static_assert(sizeof(float) == sizeof(uint32_t));
      std::memcpy(&f, &bits, sizeof(float));
      out = static_cast<double>(f);
      return true;
    }
    case 12: {  // DOUBLE
      const uint64_t bits = ReadInt<uint64_t>(ptr, little_endian);
      double d = 0.0;
      static_assert(sizeof(double) == sizeof(uint64_t));
      std::memcpy(&d, &bits, sizeof(double));
      out = d;
      return true;
    }
    default:
      return false;
  }
}

/// Read an arbitrary byte blob into a std::string (may contain NULs).
bool ReadTagBlobString(int fd, size_t file_size, const IfdEntry& entry,
                       bool bigtiff, std::string& out) {
  if (entry.count == 0) {
    out.clear();
    return true;
  }
  if (entry.type == 0 ||
      entry.type >= sizeof(kTypeSizes) / sizeof(kTypeSizes[0])) {
    return false;
  }

  const size_t type_size = kTypeSizes[entry.type];
  const size_t total_size = type_size * entry.count;
  const size_t inline_limit = bigtiff ? 8 : 4;

  std::vector<uint8_t> data;
  if (total_size <= inline_limit) {
    data.resize(total_size);
    for (size_t i = 0; i < total_size; ++i) {
      data[i] = static_cast<uint8_t>((entry.value_offset >> (i * 8)) & 0xFF);
    }
  } else {
    if (!ReadBytes(fd, file_size, entry.value_offset, total_size, data)) {
      return false;
    }
  }
  out.assign(reinterpret_cast<const char*>(data.data()), data.size());
  return true;
}

[[nodiscard]] bool ReadLazyArrayU64(int file_descriptor, size_t file_size,
                                    bool bigtiff, bool little_endian,
                                    const LazyArrayInfo& lazy,
                                    std::vector<uint64_t>& out) {
  if (lazy.count == 0) {
    out.clear();
    return true;
  }
  // NDPI extension: a single LONG-typed tag value can exceed 32-bit. In that
  // case, the high 32-bit word is patched into lazy.value_offset and must be
  // returned as a full 64-bit value (instead of only returning the low 4 bytes
  // from the "inline" ClassicTIFF slot).
  if (!bigtiff && lazy.tag_type == 4 /* LONG */ && lazy.count == 1 &&
      lazy.value_offset > 0xFFFFFFFFULL) {
    out.clear();
    out.push_back(lazy.value_offset);
    return true;
  }
  IfdEntry entry;
  entry.type = lazy.tag_type;
  entry.count = lazy.count;
  entry.value_offset = lazy.value_offset;
  return ReadTagData(file_descriptor, file_size, entry, bigtiff, little_endian,
                     out);
}

// (Removed) InferNdpiTileGeometry: we now derive tile geometry directly from
// the JPEG header (DRI + SOF sampling), matching OpenSlide's approach.

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
    const uint32_t low = ReadInt<uint32_t>(header + 4, result.little_endian);
    result.first_ifd_offset = low;

    // NDPI extension: ClassicTIFF + file >4GB can store an additional high
    // 32-bit word for the first IFD offset immediately after the standard
    // header (i.e. at bytes 8..11).
    if (static_cast<uint64_t>(file_size) > 0xFFFFFFFFULL) {
      const uint32_t high = ReadInt<uint32_t>(header + 8, result.little_endian);
      const uint64_t candidate =
          (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);

      // Heuristic validation: only treat this as NDPI if the high word is
      // non-zero and the resulting offset is within the file.
      if (high != 0 && candidate < static_cast<uint64_t>(file_size)) {
        result.ndpi_classictiff_64bit = true;
        result.first_ifd_offset = candidate;
      }
    }
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
    case kTagSoftware:
      ReadTagString(fd, file_size, entry, bigtiff, ctx.page_header.software);
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
    case kTagOffsetHighBytes:
      // NDPI extension: store high 32 bits for each offset element.
      ctx.lazy_offset_high_bytes.tag_type = entry.type;
      ctx.lazy_offset_high_bytes.count = entry.count;
      ctx.lazy_offset_high_bytes.value_offset = entry.value_offset;
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
    case kTagSubIFDs:
      // SubIFDs: array of IFD offsets pointing to reduced-resolution images,
      // overviews, etc. We treat them as additional pages.
      if (ReadTagData(fd, file_size, entry, bigtiff, little_endian, values) &&
          !values.empty()) {
        ctx.sub_ifd_offsets = std::move(values);
      }
      break;
    case kTagNdpiSourceLens: {
      double value = 0.0;
      if (ReadTagSignedDouble(fd, file_size, entry, bigtiff, little_endian,
                              value)) {
        ctx.page_header.ndpi_source_lens = value;
      }
      break;
    }
    case kTagNdpiMetadata:
      // Usually ASCII or UNDEFINED; store as blob to preserve original bytes.
      if (entry.type == 2) {
        ReadTagString(fd, file_size, entry, bigtiff,
                      ctx.page_header.ndpi_metadata);
      } else {
        ReadTagBlobString(fd, file_size, entry, bigtiff,
                          ctx.page_header.ndpi_metadata);
      }
      break;
    case kTagNdpiMcuStarts:
      // NDPI extension: per-tile start offsets within the single-strip JPEG
      // bitstream (relative to StripOffsets[0]).
      ctx.lazy_ndpi_mcu_starts.tag_type = entry.type;
      ctx.lazy_ndpi_mcu_starts.count = entry.count;
      ctx.lazy_ndpi_mcu_starts.value_offset = entry.value_offset;
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
void AddStripPageFromContext(IfdContext& ctx, TiffIndex& index) {
  StripsRec strips;
  strips.rows_per_strip = ctx.rows_per_strip;
  strips.jpeg_tables_off = ctx.jpeg_tables_offset;
  strips.jpeg_tables_len = ctx.jpeg_tables_length;
  strips.lazy_offsets = ctx.lazy_strip_offsets;
  strips.lazy_bytecounts = ctx.lazy_strip_bytecounts;
  strips.lazy_offset_high_bytes = ctx.lazy_offset_high_bytes;
  strips.offsets.count = static_cast<uint32_t>(ctx.lazy_strip_offsets.count);
  strips.bytecounts.count =
      static_cast<uint32_t>(ctx.lazy_strip_bytecounts.count);

  uint32_t payload_id = index.AddStrips(std::move(strips));
  ctx.page_header.storage = Storage::kStrips;
  ctx.page_header.payload_id = payload_id;
  index.AddPage(ctx.page_header);
}

[[nodiscard]] bool TryAddNdpiMcuTiledPageFromContext(IfdContext& ctx, int fd,
                                                     size_t file_size,
                                                     bool bigtiff,
                                                     bool little_endian,
                                                     TiffIndex& index) {
  if (ctx.page_header.compression != kCompressionJpeg ||
      ctx.lazy_strip_offsets.count != 1 ||
      ctx.lazy_strip_bytecounts.count != 1 ||
      ctx.lazy_ndpi_mcu_starts.count == 0) {
    return false;
  }

  std::vector<uint64_t> strip_offsets;
  std::vector<uint64_t> strip_bytecounts;
  std::vector<uint64_t> mcu_starts;
  const bool ok_strip_offsets =
      ReadLazyArrayU64(fd, file_size, bigtiff, little_endian,
                       ctx.lazy_strip_offsets, strip_offsets);
  const bool ok_strip_bytecounts =
      ReadLazyArrayU64(fd, file_size, bigtiff, little_endian,
                       ctx.lazy_strip_bytecounts, strip_bytecounts);
  bool ok_mcu = ReadLazyArrayU64(fd, file_size, bigtiff, little_endian,
                                 ctx.lazy_ndpi_mcu_starts, mcu_starts);

  auto looks_like_mcu_starts = [](const std::vector<uint64_t>& starts) -> bool {
    if (starts.empty()) {
      return false;
    }
    // NDPI MCU_STARTS values are 32-bit, small at the start, and usually
    // monotonic but may wrap at 2^32 for very large strips.
    if (starts[0] > (1ULL << 20)) {  // typically a few hundred bytes
      return false;
    }
    uint64_t prev = (starts[0] & 0xFFFFFFFFULL);
    uint32_t wraps = 0;
    for (size_t i = 1; i < starts.size(); ++i) {
      const uint64_t cur = (starts[i] & 0xFFFFFFFFULL);
      if (cur >= (1ULL << 32U)) {
        return false;
      }
      if (cur < prev) {
        ++wraps;
        if (wraps > 8) {
          return false;
        }
      }
      prev = cur;
    }
    return true;
  };

  if (ok_mcu && !looks_like_mcu_starts(mcu_starts)) {
    ok_mcu = false;
  }

  // Note: We intentionally do not "guess" missing 64-bit high words for the
  // MCU_STARTS tag offset. NDPI ClassicTIFF provides the per-entry high-words
  // table, which we parse when ndpi_classic64 is enabled. If that parsing fails
  // or the file is corrupted, MCU tiling is not safe to infer from random file
  // offsets.

  if (!ok_strip_offsets || !ok_strip_bytecounts || !ok_mcu ||
      strip_offsets.size() != 1 || strip_bytecounts.size() != 1 ||
      mcu_starts.empty()) {
    return false;
  }

  const uint64_t strip_off0 = strip_offsets[0];
  const uint64_t strip_bc0 = strip_bytecounts[0];
  internal::NdpiMcuTileGeometry geom{};
  std::vector<uint64_t> offsets;
  std::vector<uint64_t> bytecounts;
  const bool ok_tiles = internal::BuildNdpiMcuTilesFromMcuStarts(
      fd, file_size, strip_off0, strip_bc0, ctx.page_header.width,
      ctx.page_header.height, mcu_starts, geom, offsets, bytecounts);
  if (!ok_tiles) {
    return false;
  }

  TilesRec tiles;
  tiles.tile_w = geom.tile_w;
  tiles.tile_h = geom.tile_h;
  tiles.tiles_x = geom.tiles_x;
  tiles.tiles_y = geom.tiles_y;
  tiles.jpeg_tables_off = ctx.jpeg_tables_offset;
  tiles.jpeg_tables_len = ctx.jpeg_tables_length;
  tiles.offsets = index.AppendOffsets(offsets);
  tiles.bytecounts = index.AppendBytecounts(bytecounts);
  tiles.lazy_offsets.loaded = true;
  tiles.lazy_bytecounts.loaded = true;

  uint32_t payload_id = index.AddTiles(std::move(tiles));
  ctx.page_header.storage = Storage::kTiles;
  ctx.page_header.payload_id = payload_id;
  index.AddPage(ctx.page_header);
  return true;
}

void BuildPageFromContext(IfdContext& ctx, int fd, size_t file_size,
                          bool bigtiff, bool little_endian, TiffIndex& index) {
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
    tiles.lazy_offset_high_bytes = ctx.lazy_offset_high_bytes;
    tiles.offsets.count = static_cast<uint32_t>(ctx.lazy_tile_offsets.count);
    tiles.bytecounts.count =
        static_cast<uint32_t>(ctx.lazy_tile_bytecounts.count);

    uint32_t payload_id = index.AddTiles(std::move(tiles));
    ctx.page_header.storage = Storage::kTiles;
    ctx.page_header.payload_id = payload_id;
    index.AddPage(ctx.page_header);

  } else if (TryAddNdpiMcuTiledPageFromContext(ctx, fd, file_size, bigtiff,
                                               little_endian, index)) {

  } else if (ctx.lazy_strip_offsets.count > 0) {
    // Striped storage
    AddStripPageFromContext(ctx, index);

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

bool ParseTiffImpl(int fd, size_t file_size, bool is_ndpi, TiffIndex& index) {
  // Parse TIFF header
  auto header_opt = ParseTiffHeader(fd, file_size);
  if (!header_opt) {
    return false;  // Invalid TIFF header
  }
  const TiffHeader& tiff_header = *header_opt;

  // Set format information in index
  index.SetFormat(tiff_header.bigtiff, tiff_header.little_endian, file_size);

  const bool bigtiff = tiff_header.bigtiff;
  const bool little_endian = tiff_header.little_endian;
  const bool ndpi_classic64 =
      (!bigtiff && (is_ndpi || tiff_header.ndpi_classictiff_64bit));

  // Temporary buffer for reading IFD data
  std::vector<uint8_t> ifd_buffer;

  // Parse all IFDs, including SubIFDs, in a stable order:
  // - Each main-chain IFD
  // - Its SubIFDs in the order listed
  // - Then the next main-chain IFD
  //
  // We also guard against cycles / duplicated offsets.
  struct VisitItem {
    uint64_t ifd_offset = 0;
    std::optional<uint32_t> parent_page_index;
  };

  std::unordered_set<uint64_t> visited_ifd_offsets;
  std::vector<VisitItem> to_visit;
  to_visit.push_back(VisitItem{tiff_header.first_ifd_offset, std::nullopt});

  // Temporary adjacency lists while parsing. We'll compact these into the index
  // arena at the end.
  std::vector<std::vector<uint32_t>> child_lists;

  uint32_t ifd_index = 0;
  while (!to_visit.empty()) {
    const VisitItem item = to_visit.back();
    to_visit.pop_back();
    const uint64_t ifd_offset = item.ifd_offset;

    if (ifd_offset == 0) {
      continue;
    }
    if (visited_ifd_offsets.contains(ifd_offset)) {
      continue;
    }
    visited_ifd_offsets.insert(ifd_offset);

    // Read number of entries
    const size_t count_size = bigtiff ? 8 : 2;
    std::vector<uint8_t> count_data;
    if (!ReadBytes(fd, file_size, ifd_offset, count_size, count_data)) {
      continue;
    }

    const uint64_t num_entries =
        bigtiff ? ReadInt<uint64_t>(count_data.data(), little_endian)
                : ReadInt<uint16_t>(count_data.data(), little_endian);

    // Initialize IFD context for this page
    IfdContext ctx;
    ctx.page_header.ifd_index = ifd_index;
    ctx.page_header.ifd_offset = ifd_offset;

    const size_t entry_size = bigtiff ? 20 : 12;
    const size_t offset_size = bigtiff ? 8 : (ndpi_classic64 ? 8 : 4);
    const uint64_t entries_start_offset = ifd_offset + count_size;

    // Batch read all IFD entries + next offset.
    const size_t total_entries_size = num_entries * entry_size;
    const size_t ifd_data_size = total_entries_size + offset_size;
    if (!ReadBytes(fd, file_size, entries_start_offset, ifd_data_size,
                   ifd_buffer)) {
      continue;
    }

    // Parse all IFD entries first (so we can apply NDPI high bits if present).
    std::vector<IfdEntry> entries;
    entries.reserve(static_cast<size_t>(num_entries));
    for (uint64_t i = 0; i < num_entries; ++i) {
      const uint8_t* entry_data = ifd_buffer.data() + (i * entry_size);

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
      entries.push_back(entry);
    }

    // NDPI ClassicTIFF extension: read per-entry high 32-bit words for
    // entry.value_offset. On NDPI ClassicTIFF, the next-IFD offset is 8 bytes
    // and the high-words table follows after that.
    if (ndpi_classic64 && num_entries > 0) {
      const uint64_t high_array_offset =
          entries_start_offset +
          static_cast<uint64_t>(total_entries_size + offset_size);
      std::vector<uint8_t> high_bytes;
      const uint64_t high_array_size = num_entries * 4;
      const bool ok_high_bytes = ReadBytes(fd, file_size, high_array_offset,
                                           high_array_size, high_bytes) &&
                                 high_bytes.size() == high_array_size;
      if (!ok_high_bytes) {
        high_bytes.clear();
      }
      const size_t inline_limit = 4;
      if (ok_high_bytes) {
        for (uint64_t i = 0; i < num_entries; ++i) {
          const uint32_t high =
              ReadInt<uint32_t>(high_bytes.data() + i * 4, little_endian);
          if (high == 0) {
            continue;
          }

          // Compute total tag data size to decide whether value_offset is an
          // out-of-line pointer.
          const uint16_t type = entries[static_cast<size_t>(i)].type;
          if (type == 0 || type >= kTypeSizes.size()) {
            continue;
          }
          const uint64_t total_size = static_cast<uint64_t>(kTypeSizes[type]) *
                                      entries[static_cast<size_t>(i)].count;

          const bool is_out_of_line = total_size > inline_limit;
          const bool inline_is_offset_value =
              (entries[static_cast<size_t>(i)].tag == kTagStripOffsets) ||
              (entries[static_cast<size_t>(i)].tag == kTagTileOffsets) ||
              (entries[static_cast<size_t>(i)].tag == kTagJpegTables) ||
              (entries[static_cast<size_t>(i)].tag == kTagSubIFDs);

          // NDPI extension: some inline LONG-typed tag values can exceed
          // 32-bit.
          const bool inline_is_64bit_long_value =
              (entries[static_cast<size_t>(i)].type == 4 /* LONG */) &&
              (entries[static_cast<size_t>(i)].count == 1) &&
              ((entries[static_cast<size_t>(i)].tag == kTagStripByteCounts) ||
               (entries[static_cast<size_t>(i)].tag == kTagTileByteCounts) ||
               (entries[static_cast<size_t>(i)].tag == kTagStripOffsets) ||
               (entries[static_cast<size_t>(i)].tag == kTagTileOffsets));

          if (is_out_of_line || inline_is_offset_value ||
              inline_is_64bit_long_value) {
            entries[static_cast<size_t>(i)].value_offset |=
                (static_cast<uint64_t>(high) << 32);
          }
        }
      }
    }

    // Process tags and update context.
    for (const auto& entry : entries) {
      ProcessTag(entry, fd, file_size, bigtiff, little_endian, ctx);
    }

    // Build page from accumulated context and add to index
    BuildPageFromContext(ctx, fd, file_size, bigtiff, little_endian, index);
    const uint32_t page_index = static_cast<uint32_t>(index.NumPages() - 1);

    if (child_lists.size() < index.NumPages()) {
      child_lists.resize(index.NumPages());
    }

    if (item.parent_page_index.has_value()) {
      const uint32_t parent = *item.parent_page_index;
      index.MutablePage(page_index).parent_page_index = parent;
      if (parent < child_lists.size()) {
        child_lists[parent].push_back(page_index);
      }
    }

    // Parse next IFD offset from the buffer
    const uint8_t* next_offset_data = ifd_buffer.data() + total_entries_size;
    const uint64_t next_ifd_offset =
        bigtiff ? ReadInt<uint64_t>(next_offset_data, little_endian)
                : (ndpi_classic64
                       ? ReadInt<uint64_t>(next_offset_data, little_endian)
                       : ReadInt<uint32_t>(next_offset_data, little_endian));

    ++ifd_index;

    // Traversal order for stack (LIFO):
    // push next main IFD first (processed later),
    // then push SubIFDs in reverse so the first SubIFD is processed next.
    if (next_ifd_offset != 0) {
      // Important: if this IFD is itself a SubIFD (has a parent), the next-IFD
      // pointer continues the SubIFD chain and should inherit the same parent.
      // If this IFD is a root (no parent), next-IFD continues the main IFD
      // chain.
      to_visit.push_back(VisitItem{next_ifd_offset, item.parent_page_index});
    }
    for (auto it = ctx.sub_ifd_offsets.rbegin();
         it != ctx.sub_ifd_offsets.rend(); ++it) {
      to_visit.push_back(VisitItem{*it, page_index});
    }
  }

  // Compact child lists into the index arena.
  for (size_t i = 0; i < index.NumPages(); ++i) {
    index.MutablePage(i).sub_pages = index.AppendChildPages(child_lists[i]);
  }

  return index.NumPages() > 0;
}

bool ParseTiff(int fd, uint64_t file_size, TiffIndex& index) {
  // This API does not have access to the original file path/extension, so it
  // relies on ParseTiffHeader's NDPI heuristics (rather than forcing NDPI
  // mode).
  return ParseTiffImpl(fd, static_cast<size_t>(file_size), /*is_ndpi=*/false,
                       index);
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

  const std::string path_str(filepath);
  const bool is_ndpi = (path_str.size() >= 5) &&
                       (path_str.substr(path_str.size() - 5) == ".ndpi" ||
                        path_str.substr(path_str.size() - 5) == ".NDPI");

  // Parse the TIFF file using pread
  if (!ParseTiffImpl(fd, file_size, is_ndpi, index)) {
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
    case 4:   // LONG
    case 13:  // IFD (pointer, ClassicTIFF)
      out_value = ReadInt<uint32_t>(ptr, little_endian);
      break;
    case 16:  // LONG8 (BigTIFF)
    case 18:  // IFD8 (BigTIFF)
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
    // NDPI: apply per-element high 32 bits for offsets (tag 65324) if present.
    if (tiles_rec.lazy_offset_high_bytes.count ==
            tiles_rec.lazy_offsets.count &&
        tiles_rec.lazy_offset_high_bytes.count > 0) {
      uint64_t high = 0;
      if (!ReadSingleTagElement(index.Fd(), index.FileSize(),
                                tiles_rec.lazy_offset_high_bytes,
                                index.IsBigTiff(), index.IsLittleEndian(),
                                tile_or_strip_index, high)) {
        return false;
      }
      out_offset |= (high << 32);
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
    // NDPI: apply per-element high 32 bits for offsets (tag 65324) if present.
    if (strips_rec.lazy_offset_high_bytes.count ==
            strips_rec.lazy_offsets.count &&
        strips_rec.lazy_offset_high_bytes.count > 0) {
      uint64_t high = 0;
      if (!ReadSingleTagElement(index.Fd(), index.FileSize(),
                                strips_rec.lazy_offset_high_bytes,
                                index.IsBigTiff(), index.IsLittleEndian(),
                                tile_or_strip_index, high)) {
        return false;
      }
      out_offset |= (high << 32);
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

      // NDPI: combine high 32 bits for each offset element (tag 65324).
      if (tiles_rec.lazy_offset_high_bytes.count ==
              tiles_rec.lazy_offsets.count &&
          tiles_rec.lazy_offset_high_bytes.count > 0) {
        IfdEntry high_entry;
        high_entry.type = tiles_rec.lazy_offset_high_bytes.tag_type;
        high_entry.count = tiles_rec.lazy_offset_high_bytes.count;
        high_entry.value_offset = tiles_rec.lazy_offset_high_bytes.value_offset;
        std::vector<uint64_t> high_vals;
        if (!ReadTagData(index.Fd(), index.FileSize(), high_entry,
                         index.IsBigTiff(), index.IsLittleEndian(),
                         high_vals)) {
          return false;
        }
        if (high_vals.size() == offsets.size()) {
          for (size_t i = 0; i < offsets.size(); ++i) {
            offsets[i] |= (high_vals[i] << 32);
          }
        }
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

      // NDPI: combine high 32 bits for each offset element (tag 65324).
      if (strips_rec.lazy_offset_high_bytes.count ==
              strips_rec.lazy_offsets.count &&
          strips_rec.lazy_offset_high_bytes.count > 0) {
        IfdEntry high_entry;
        high_entry.type = strips_rec.lazy_offset_high_bytes.tag_type;
        high_entry.count = strips_rec.lazy_offset_high_bytes.count;
        high_entry.value_offset =
            strips_rec.lazy_offset_high_bytes.value_offset;
        std::vector<uint64_t> high_vals;
        if (!ReadTagData(index.Fd(), index.FileSize(), high_entry,
                         index.IsBigTiff(), index.IsLittleEndian(),
                         high_vals)) {
          return false;
        }
        if (high_vals.size() == offsets.size()) {
          for (size_t i = 0; i < offsets.size(); ++i) {
            offsets[i] |= (high_vals[i] << 32);
          }
        }
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
