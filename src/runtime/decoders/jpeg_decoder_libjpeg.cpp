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

#include "fastslide/runtime/decoders/jpeg_decoder.h"

#include <array>
#include <csetjmp>
#include <cstdint>
#include <cstring>

#include <jpeglib.h>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide::runtime::decoders {
namespace {

struct ThreadLocalJpeg {
  jpeg_decompress_struct cinfo;
  jpeg_error_mgr jerr;
  std::jmp_buf jump_buffer;
  char error_message[JMSG_LENGTH_MAX] = {0};
  bool inited = false;

  ~ThreadLocalJpeg() {
    if (inited) {
      jpeg_destroy_decompress(&cinfo);
    }
  }

  static void ErrorExit(j_common_ptr cinfo) {
    auto* self = reinterpret_cast<ThreadLocalJpeg*>(cinfo->client_data);
    (*cinfo->err->format_message)(cinfo, self->error_message);
    std::longjmp(self->jump_buffer, 1);
  }

  jpeg_decompress_struct* Get() {
    if (!inited) {
      cinfo.err = jpeg_std_error(&jerr);
      jerr.error_exit = ErrorExit;
      cinfo.client_data = this;
      jpeg_create_decompress(&cinfo);
      inited = true;
    } else {
      jpeg_abort_decompress(&cinfo);
    }
    return &cinfo;
  }
};

static thread_local ThreadLocalJpeg g_tls_jpeg;

}  // namespace

aifocore::Result<DecodedRgb> DecodeJpegToRgb(
    std::span<const uint8_t> jpeg_bytes, const JpegDecodeOptions& options) {
  (void)options;  // libjpeg path always converts to RGB.
  if (jpeg_bytes.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "JPEG input is empty");
  }

  jpeg_decompress_struct* c = g_tls_jpeg.Get();
  if (setjmp(g_tls_jpeg.jump_buffer)) {
    jpeg_abort_decompress(c);
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            aifocore::fmt::format("JPEG decode error: {}",
                                                  g_tls_jpeg.error_message));
  }

  jpeg_mem_src(c, const_cast<unsigned char*>(jpeg_bytes.data()),
               static_cast<unsigned long>(jpeg_bytes.size()));
  if (jpeg_read_header(c, TRUE) != JPEG_HEADER_OK) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to read JPEG header");
  }

  c->dct_method = JDCT_IFAST;
  c->do_fancy_upsampling = FALSE;
  c->do_block_smoothing = FALSE;
  c->quantize_colors = FALSE;
  c->dither_mode = JDITHER_NONE;

#ifdef JCS_EXT_RGB
  c->out_color_space = JCS_EXT_RGB;
#else
  c->out_color_space = JCS_RGB;
#endif

  if (!jpeg_start_decompress(c)) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to start JPEG decompression");
  }

  const uint32_t width = static_cast<uint32_t>(c->output_width);
  const uint32_t height = static_cast<uint32_t>(c->output_height);
  const int channels = static_cast<int>(c->output_components);
  if (width == 0 || height == 0 || channels != 3) {
    jpeg_abort_decompress(c);
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Unsupported JPEG output format");
  }

  DecodedRgb out{};
  out.width = width;
  out.height = height;
  out.rgb.resize(static_cast<size_t>(width) * height * 3);

  // Batch read scanlines to reduce call overhead.
  constexpr JDIMENSION kBatch = 32;
  std::array<JSAMPROW, kBatch> rows;
  const JDIMENSION row_stride = static_cast<JDIMENSION>(width * 3);
  while (c->output_scanline < c->output_height) {
    const JDIMENSION start = c->output_scanline;
    const JDIMENSION remain = c->output_height - start;
    const JDIMENSION n = (std::min)(remain, kBatch);
    for (JDIMENSION i = 0; i < n; ++i) {
      rows[i] = out.rgb.data() + (static_cast<size_t>(start + i) * row_stride);
    }
    const JDIMENSION got = jpeg_read_scanlines(c, rows.data(), n);
    if (got == 0) {
      jpeg_abort_decompress(c);
      return aifocore::Status(aifocore::StatusCode::kInternal,
                              "JPEG read_scanlines returned 0");
    }
  }

  jpeg_finish_decompress(c);
  return out;
}

}  // namespace fastslide::runtime::decoders
