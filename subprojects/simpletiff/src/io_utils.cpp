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
#include "simpletiff/io_utils.h"

#include <cstdio>

#include <jpeglib.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "simpletiff/reader.h"

namespace simpletiff {

// =============================================================================
// DecodeContext Implementation
// =============================================================================

DecodeContext::DecodeContext() = default;

DecodeContext::~DecodeContext() {
  if (jpeg_cinfo) {
    jpeg_destroy_decompress(jpeg_cinfo);
    delete jpeg_cinfo;
    delete jpeg_err;
  }
}

DecodeContext::DecodeContext(DecodeContext&& other) noexcept
    : jpeg_stream_buffer(std::move(other.jpeg_stream_buffer)),
      temp_buffer(std::move(other.temp_buffer)),
      jpeg_cinfo(other.jpeg_cinfo),
      jpeg_err(other.jpeg_err) {
  other.jpeg_cinfo = nullptr;
  other.jpeg_err = nullptr;
}

DecodeContext& DecodeContext::operator=(DecodeContext&& other) noexcept {
  if (this != &other) {
    // Clean up existing resources
    if (jpeg_cinfo) {
      jpeg_destroy_decompress(jpeg_cinfo);
      delete jpeg_cinfo;
      delete jpeg_err;
    }

    // Move from other
    jpeg_stream_buffer = std::move(other.jpeg_stream_buffer);
    temp_buffer = std::move(other.temp_buffer);
    jpeg_cinfo = other.jpeg_cinfo;
    jpeg_err = other.jpeg_err;

    other.jpeg_cinfo = nullptr;
    other.jpeg_err = nullptr;
  }
  return *this;
}

namespace {

// Custom JPEG error handler that suppresses warnings
void silent_jpeg_output_message(j_common_ptr cinfo) {
  // Do nothing - suppress all warnings
  (void)cinfo;
}

/// Initialize JPEG decompressor in context (lazy initialization)
void EnsureJpegDecompressor(DecodeContext& ctx) {
  // Reuse decompressor for better performance
  if (!ctx.jpeg_cinfo) {
    ctx.jpeg_err = new jpeg_error_mgr;
    ctx.jpeg_cinfo = new jpeg_decompress_struct;
    ctx.jpeg_cinfo->err = jpeg_std_error(ctx.jpeg_err);
    // Suppress warnings by overriding the output_message callback
    ctx.jpeg_err->output_message = silent_jpeg_output_message;
    jpeg_create_decompress(ctx.jpeg_cinfo);
  } else {
    // Reset for reuse
    jpeg_abort_decompress(ctx.jpeg_cinfo);
  }
}

}  // namespace

bool ReadBytes(int fd, size_t file_size, uint64_t offset, uint64_t length,
               std::vector<uint8_t>& out, bool strict) {
  if (length == 0) {
    out.clear();
    return true;
  }

  // Check bounds
  if (fd < 0) {
    return false;
  }

  const uint64_t file_sz = static_cast<uint64_t>(file_size);

  // In strict mode, we validate against file size before reading
  if (strict) {
    if (offset > file_sz || length > file_sz - offset) {
      return false;
    }
  } else {
    // In loose mode, we clamp logic.
    // If offset is beyond file size, we return empty (success) because there's
    // nothing to read, but it's not an I/O error per se unless caller expects
    // data.
    if (offset >= file_sz) {
      out.clear();
      return true;
    }
    // Clamp length to available bytes
    if (length > file_sz - offset) {
      length = file_sz - offset;
    }
  }

  if (length == 0) {
    out.clear();
    return true;
  }

  // Resize buffer to fit requested (or clamped) data
  out.resize(static_cast<size_t>(length));

  // Use portable pread
  const ssize_t bytes_read = aifocore::portable_pread(
      fd, out.data(), static_cast<size_t>(length), offset);

  if (bytes_read < 0) {
    return false;
  }

  // Check if we got what we asked for
  if (static_cast<size_t>(bytes_read) != length) {
    if (strict) {
      return false;
    }
    // In loose mode, if we got fewer bytes than expected (and fewer than
    // clamped), resize to actual.
    out.resize(static_cast<size_t>(bytes_read));
  }

  return true;
}

std::span<const uint8_t> ReadBytesSpan(int fd, size_t file_size,
                                       uint64_t offset, uint64_t length,
                                       std::vector<uint8_t>& buffer,
                                       bool strict) {
  // Read into the provided buffer
  if (!ReadBytes(fd, file_size, offset, length, buffer, strict)) {
    return {};
  }

  // Return span to buffer
  return {buffer.data(), buffer.size()};
}

void ComposeJpegStream(std::span<const uint8_t> tables,
                       std::span<const uint8_t> payload,
                       std::vector<uint8_t>& out) {
  constexpr uint8_t kSoi[2] = {0xFF, 0xD8};
  constexpr uint8_t kEoi[2] = {0xFF, 0xD9};

  auto starts_with = [](std::span<const uint8_t> s,
                        const uint8_t* marker) -> bool {
    return s.size() >= 2 && s[0] == marker[0] && s[1] == marker[1];
  };

  auto ends_with = [](std::span<const uint8_t> s,
                      const uint8_t* marker) -> bool {
    return s.size() >= 2 && s[s.size() - 2] == marker[0] &&
           s[s.size() - 1] == marker[1];
  };

  // Check for SOI/EOI in both tables and payload
  const bool tables_has_soi = starts_with(tables, kSoi);
  const bool tables_has_eoi = ends_with(tables, kEoi);
  const bool payload_has_soi = starts_with(payload, kSoi);
  const bool payload_has_eoi = ends_with(payload, kEoi);

  // Calculate output size
  size_t out_sz = 2 /*SOI*/ + tables.size() + payload.size() + 2 /*EOI*/;
  if (tables_has_soi)
    out_sz -= 2;
  if (tables_has_eoi)
    out_sz -= 2;
  if (payload_has_soi)
    out_sz -= 2;
  if (payload_has_eoi)
    out_sz -= 2;

  // Resize once (avoids reallocations)
  out.resize(out_sz);
  uint8_t* ptr = out.data();

  // Copy SOI marker
  std::memcpy(ptr, kSoi, 2);
  ptr += 2;

  // Copy tables (strip SOI/EOI if present)
  const size_t tables_start = tables_has_soi ? 2 : 0;
  const size_t tables_len =
      tables.size() - tables_start - (tables_has_eoi ? 2 : 0);
  if (tables_len > 0) {
    std::memcpy(ptr, tables.data() + tables_start, tables_len);
    ptr += tables_len;
  }

  // Copy payload (strip SOI/EOI if present)
  const size_t payload_start = payload_has_soi ? 2 : 0;
  const size_t payload_len =
      payload.size() - payload_start - (payload_has_eoi ? 2 : 0);
  if (payload_len > 0) {
    std::memcpy(ptr, payload.data() + payload_start, payload_len);
    ptr += payload_len;
  }

  // Copy EOI marker
  std::memcpy(ptr, kEoi, 2);
}

bool DecodeJpeg(DecodeContext& ctx, std::span<const uint8_t> jpeg_data,
                int& out_width, int& out_height, std::vector<uint8_t>& out_rgb,
                const JpegDecodeOptions& options) {
  if (jpeg_data.empty()) {
    return false;
  }

  // Lazily initialize or reset JPEG decompressor
  EnsureJpegDecompressor(ctx);
  jpeg_decompress_struct& cinfo = *ctx.jpeg_cinfo;

  // Use memory source
  jpeg_mem_src(&cinfo, const_cast<uint8_t*>(jpeg_data.data()),
               static_cast<size_t>(jpeg_data.size()));

  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    return false;
  }

  // Handle color space conversion.
  //
  // If treat_ycbcr_as_rgb=true, we disable YCbCr->RGB conversion by telling
  // libjpeg the input is RGB (legacy compatibility with some TIFF JPEG
  // variants).
  //
  // If treat_ycbcr_as_rgb=false, we allow libjpeg to convert YCbCr->RGB.
  if (cinfo.jpeg_color_space == JCS_YCbCr) {
    if (options.treat_ycbcr_as_rgb) {
      cinfo.jpeg_color_space = JCS_RGB;
      cinfo.out_color_space = JCS_RGB;
    } else {
      cinfo.out_color_space = JCS_RGB;
    }
  } else if (cinfo.jpeg_color_space == JCS_RGB) {
    cinfo.out_color_space = JCS_RGB;
  } else if (cinfo.jpeg_color_space == JCS_GRAYSCALE) {
    cinfo.out_color_space = JCS_GRAYSCALE;
  } else {
    cinfo.out_color_space = JCS_RGB;
  }

  cinfo.dct_method = JDCT_IFAST;      // Fast integer DCT
  cinfo.do_fancy_upsampling = FALSE;  // Speed win on YCbCr 4:2:0
  cinfo.do_block_smoothing = FALSE;   // Disable smoothing for speed

  if (jpeg_start_decompress(&cinfo) != TRUE) {
    jpeg_abort_decompress(&cinfo);
    return false;
  }

  out_width = static_cast<int>(cinfo.output_width);
  out_height = static_cast<int>(cinfo.output_height);

  // Compute row stride based on actual output components (1 for grayscale, 3
  // for RGB)
  const size_t row_stride = static_cast<size_t>(cinfo.output_width) *
                            static_cast<size_t>(cinfo.output_components);
  out_rgb.resize(row_stride * cinfo.output_height);

  // Read scanlines
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row_pointer =
        out_rgb.data() +
        static_cast<size_t>(cinfo.output_scanline) * row_stride;
    if (jpeg_read_scanlines(&cinfo, &row_pointer, 1) != 1) {
      jpeg_abort_decompress(&cinfo);
      return false;
    }
  }

  if (jpeg_finish_decompress(&cinfo) != TRUE) {
    jpeg_abort_decompress(&cinfo);
    return false;
  }

  return true;
}

void CopyTileInto(uint8_t* dst, int dst_stride, const uint8_t* tile_data,
                  int tile_width, int tile_height, int dst_x, int dst_y,
                  int roi_width, int roi_height, int samples_per_pixel) {
  // Clip tile placement against ROI bounds, including negative offsets.
  const int src_start_x = std::max(0, -dst_x);
  const int src_start_y = std::max(0, -dst_y);
  const int dst_start_x = std::max(0, dst_x);
  const int dst_start_y = std::max(0, dst_y);

  const int max_copy_w = tile_width - src_start_x;
  const int max_copy_h = tile_height - src_start_y;
  const int roi_copy_w = roi_width - dst_start_x;
  const int roi_copy_h = roi_height - dst_start_y;

  const int w_copy = std::min(max_copy_w, roi_copy_w);
  const int h_copy = std::min(max_copy_h, roi_copy_h);

  if (w_copy <= 0 || h_copy <= 0) {
    return;
  }

  const int bytes_per_pixel = samples_per_pixel;  // 8-bit/sample assumption
  for (int r = 0; r < h_copy; ++r) {
    const uint8_t* src_row = tile_data +
                             (src_start_y + r) * tile_width * bytes_per_pixel +
                             src_start_x * bytes_per_pixel;
    uint8_t* dst_row =
        dst + (dst_start_y + r) * dst_stride + dst_start_x * bytes_per_pixel;
    std::memcpy(dst_row, src_row,
                static_cast<size_t>(w_copy) * bytes_per_pixel);
  }
}

}  // namespace simpletiff
