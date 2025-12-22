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

// I/O utilities with JPEG decompression using jpeg-compressor (jpgd)

#include "simpletiff/io_utils.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "jpeg-compressor/jpgd.h"
#include "simpletiff/reader.h"

namespace simpletiff {

// =============================================================================
// DecodeContext Implementation (jpgd version - no JPEG state needed)
// =============================================================================

DecodeContext::DecodeContext() = default;
DecodeContext::~DecodeContext() = default;

DecodeContext::DecodeContext(DecodeContext&& other) noexcept
    : jpeg_stream_buffer(std::move(other.jpeg_stream_buffer)),
      temp_buffer(std::move(other.temp_buffer)),
      jpeg_cinfo(nullptr),
      jpeg_err(nullptr) {}

DecodeContext& DecodeContext::operator=(DecodeContext&& other) noexcept {
  if (this != &other) {
    jpeg_stream_buffer = std::move(other.jpeg_stream_buffer);
    temp_buffer = std::move(other.temp_buffer);
  }
  return *this;
}

// =============================================================================
// File I/O
// =============================================================================

bool ReadBytes(int fd, size_t file_size, uint64_t offset, uint64_t length,
               std::vector<uint8_t>& out) {
  if (fd < 0 || offset + length > file_size) {
    return false;
  }

  out.resize(length);
  ssize_t bytes_read = aifocore::portable_pread(fd, out.data(), length, offset);

  if (bytes_read < 0 || static_cast<size_t>(bytes_read) != length) {
    return false;
  }

  return true;
}

std::span<const uint8_t> ReadBytesSpan(int fd, size_t file_size,
                                       uint64_t offset, uint64_t length,
                                       std::vector<uint8_t>& buffer) {
  if (!ReadBytes(fd, file_size, offset, length, buffer)) {
    return {};
  }
  return std::span<const uint8_t>(buffer.data(), buffer.size());
}

// =============================================================================
// JPEG Stream Composition
// =============================================================================

void ComposeJpegStream(std::span<const uint8_t> tables,
                       std::span<const uint8_t> payload,
                       std::vector<uint8_t>& out) {
  constexpr uint8_t kSoi[] = {0xFF, 0xD8};
  constexpr uint8_t kEoi[] = {0xFF, 0xD9};

  // Check if tables have SOI at start and EOI at end
  const bool tables_has_soi =
      tables.size() >= 2 && tables[0] == 0xFF && tables[1] == 0xD8;
  const bool tables_has_eoi = tables.size() >= 2 &&
                              tables[tables.size() - 2] == 0xFF &&
                              tables[tables.size() - 1] == 0xD9;

  // Check if payload already starts with SOI
  const bool payload_has_soi =
      payload.size() >= 2 && payload[0] == 0xFF && payload[1] == 0xD8;
  const bool payload_has_eoi = payload.size() >= 2 &&
                               payload[payload.size() - 2] == 0xFF &&
                               payload[payload.size() - 1] == 0xD9;

  const size_t payload_start = payload_has_soi ? 2 : 0;

  // Compute final size
  size_t total_size = 2;  // SOI
  if (!tables.empty()) {
    size_t table_size = tables.size();
    if (tables_has_soi)
      table_size -= 2;
    if (tables_has_eoi)
      table_size -= 2;
    total_size += table_size;
  }
  total_size += payload.size() - payload_start - (payload_has_eoi ? 2 : 0);
  total_size += 2;  // EOI

  out.resize(total_size);
  uint8_t* ptr = out.data();

  // Copy SOI
  std::memcpy(ptr, kSoi, 2);
  ptr += 2;

  // Copy tables (skip SOI at start and EOI at end if present)
  if (!tables.empty()) {
    const size_t table_offset = tables_has_soi ? 2 : 0;
    size_t table_len = tables.size() - table_offset;
    if (tables_has_eoi)
      table_len -= 2;
    if (table_len > 0) {
      std::memcpy(ptr, tables.data() + table_offset, table_len);
      ptr += table_len;
    }
  }

  // Copy payload (skip SOI and EOI if present)
  const size_t payload_len =
      payload.size() - payload_start - (payload_has_eoi ? 2 : 0);
  if (payload_len > 0) {
    std::memcpy(ptr, payload.data() + payload_start, payload_len);
    ptr += payload_len;
  }

  // Copy EOI marker
  std::memcpy(ptr, kEoi, 2);
}

// =============================================================================
// JPEG Decoding (jpgd version)
// =============================================================================

bool DecodeJpeg(DecodeContext& ctx, std::span<const uint8_t> jpeg_data,
                int& out_width, int& out_height, std::vector<uint8_t>& out_rgb,
                const JpegDecodeOptions& options) {
  if (jpeg_data.empty()) {
    return false;
  }

  // jpgd will allocate and decompress the image
  int actual_comps = 0;
  int width = 0;
  int height = 0;

  const uint32_t flags = options.treat_ycbcr_as_rgb
                             ? jpgd::jpeg_decoder::cFlagNoYCbCrConversion
                             : 0u;

  // Request 3 components (RGB)
  unsigned char* decoded = jpgd::decompress_jpeg_image_from_memory(
      jpeg_data.data(), static_cast<int>(jpeg_data.size()), &width, &height,
      &actual_comps, 3, flags);

  if (!decoded) {
    return false;
  }

  // Set output dimensions
  out_width = width;
  out_height = height;

  // jpgd returns data in the requested format
  // When req_comps=3, it returns RGB (3 bytes per pixel)
  const size_t num_pixels =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  out_rgb.resize(num_pixels * 3);
  std::memcpy(out_rgb.data(), decoded, num_pixels * 3);

  // Free jpgd's allocated memory (using standard free since jpgd uses malloc)
  free(decoded);

  return true;
}

// =============================================================================
// Tile copying
// =============================================================================

void CopyTileInto(uint8_t* dst, int dst_stride, const uint8_t* tile_data,
                  int tile_width, int tile_height, int dst_x, int dst_y,
                  int roi_width, int roi_height, int samples_per_pixel) {
  const int w_copy = std::min(tile_width, roi_width - dst_x);
  const int h_copy = std::min(tile_height, roi_height - dst_y);

  if (w_copy <= 0 || h_copy <= 0) {
    return;
  }

  for (int r = 0; r < h_copy; ++r) {
    const uint8_t* src_row = tile_data + r * tile_width * samples_per_pixel;
    uint8_t* dst_row =
        dst + (dst_y + r) * dst_stride + (dst_x * samples_per_pixel);
    std::memcpy(dst_row, src_row, w_copy * samples_per_pixel);
  }
}

}  // namespace simpletiff
