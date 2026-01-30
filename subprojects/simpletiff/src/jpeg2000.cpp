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

#include "simpletiff/jpeg2000.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "aifocore/status/result.h"

#if defined(SIMPLETIFF_HAS_OPENJPEG)
extern "C" {
#include "openjpeg.h"
}  // extern "C"
#endif  // SIMPLETIFF_HAS_OPENJPEG

// =============================================================================
// Third-party attribution
// =============================================================================
//
// This file contains a behavioral port of key JPEG2000 post-processing logic
// from OpenJPEG 2.4.0 (https://github.com/uclouvain/openjpeg), specifically:
// - openjpeg-2.4.0/src/bin/common/color.c: color_sycc_to_rgb() (sYCC -> RGB)
// - openjpeg-2.4.0/src/bin/jp2/opj_decompress.c: upsample_image_components()
//
// OpenJPEG is licensed under the BSD 2-Clause License.
// See: openjpeg-2.4.0/LICENSE (in this repository).
//
// The port is adapted to SimpleTIFF's requirements:
// - Produces an interleaved output buffer (RGBRGB...) instead of mutating an
//   opj_image_t in-place.
// - Uses aifocore::Result for error handling.

namespace simpletiff {

// using aifocore::Error;
using aifocore::Result;
using aifocore::StatusCode;

namespace {

constexpr bool IsJp2(std::span<const uint8_t> data) {
  // JP2 signature box:
  // 00 00 00 0C 6A 50 20 20 0D 0A 87 0A
  constexpr uint8_t kSig[] = {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50,
                              0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A};
  if (data.size() < sizeof(kSig)) {
    return false;
  }
  return std::memcmp(data.data(), kSig, sizeof(kSig)) == 0;
}

constexpr bool IsJ2kCodestream(std::span<const uint8_t> data) {
  // SOC marker 0xFF4F at start.
  return data.size() >= 2 && data[0] == 0xFF && data[1] == 0x4F;
}

template <typename T>
inline T ClampTo(T v, T lo, T hi) {
  return std::min(std::max(v, lo), hi);
}

}  // namespace

#if !defined(SIMPLETIFF_HAS_OPENJPEG)

Result<void> DecodeJpeg2000(std::span<const uint8_t> /*compressed*/,
                            bool /*file_big_endian*/,
                            uint16_t /*expected_bits_per_sample*/,
                            uint16_t /*expected_samples_per_pixel*/,
                            bool /*convert_ycbcr_to_rgb*/, int& /*out_width*/,
                            int& /*out_height*/,
                            std::vector<uint8_t>& /*out*/) {
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kUnimplemented,
      "JPEG2000 support is not enabled in this build (missing OpenJPEG)");
}

#else

namespace {

struct InputMemoryStream {
  const OPJ_BYTE* data = nullptr;
  OPJ_SIZE_T size = 0;
  OPJ_SIZE_T pos = 0;
};

OPJ_SIZE_T ReadMemory(void* p_buffer, OPJ_SIZE_T p_nb_bytes,
                      void* p_user_data) {
  auto* s = static_cast<InputMemoryStream*>(p_user_data);
  if (s == nullptr || s->data == nullptr) {
    return static_cast<OPJ_SIZE_T>(-1);
  }
  const OPJ_SIZE_T remaining = (s->pos < s->size) ? (s->size - s->pos) : 0;
  const OPJ_SIZE_T to_read = std::min(p_nb_bytes, remaining);
  if (to_read == 0) {
    return 0;
  }
  std::memcpy(p_buffer, s->data + s->pos, to_read);
  s->pos += to_read;
  return to_read;
}

OPJ_OFF_T SkipMemory(OPJ_OFF_T p_nb_bytes, void* p_user_data) {
  auto* s = static_cast<InputMemoryStream*>(p_user_data);
  if (s == nullptr) {
    return static_cast<OPJ_OFF_T>(-1);
  }
  const OPJ_OFF_T remaining =
      (s->pos < s->size) ? static_cast<OPJ_OFF_T>(s->size - s->pos) : 0;
  const OPJ_OFF_T to_skip = std::min(p_nb_bytes, remaining);
  if (to_skip < 0) {
    return static_cast<OPJ_OFF_T>(-1);
  }
  s->pos += static_cast<OPJ_SIZE_T>(to_skip);
  return to_skip;
}

OPJ_BOOL SeekMemory(OPJ_OFF_T p_nb_bytes, void* p_user_data) {
  auto* s = static_cast<InputMemoryStream*>(p_user_data);
  if (s == nullptr) {
    return OPJ_FALSE;
  }
  if (p_nb_bytes < 0) {
    return OPJ_FALSE;
  }
  const auto target = static_cast<OPJ_SIZE_T>(p_nb_bytes);
  if (target > s->size) {
    return OPJ_FALSE;
  }
  s->pos = target;
  return OPJ_TRUE;
}

void FreeInputMemory(void* p_user_data) {
  delete static_cast<InputMemoryStream*>(p_user_data);
}

struct OpenJpegLog {
  std::string first_error;
};

void ErrorCallback(const char* msg, void* user_data) {
  auto* log = static_cast<OpenJpegLog*>(user_data);
  if (log == nullptr || msg == nullptr) {
    return;
  }
  if (log->first_error.empty()) {
    log->first_error = msg;
    // Trim trailing newlines for nicer error messages.
    while (!log->first_error.empty() && (log->first_error.back() == '\n' ||
                                         log->first_error.back() == '\r')) {
      log->first_error.pop_back();
    }
  }
}

Result<opj_image_t*> DecodeOpenJpeg(std::span<const uint8_t> compressed,
                                    OPJ_CODEC_FORMAT format) {
  opj_stream_t* stream = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_TRUE);
  if (stream == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: failed to create input stream");
  }

  auto* mem = new InputMemoryStream();
  mem->data = reinterpret_cast<const OPJ_BYTE*>(compressed.data());
  mem->size = static_cast<OPJ_SIZE_T>(compressed.size());
  mem->pos = 0;

  opj_stream_set_user_data(stream, mem, FreeInputMemory);
  opj_stream_set_user_data_length(stream, static_cast<OPJ_UINT64>(mem->size));
  opj_stream_set_read_function(stream, ReadMemory);
  opj_stream_set_skip_function(stream, SkipMemory);
  opj_stream_set_seek_function(stream, SeekMemory);

  opj_codec_t* codec = opj_create_decompress(format);
  if (codec == nullptr) {
    opj_stream_destroy(stream);
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: failed to create decompressor");
  }

  OpenJpegLog log;
  (void)opj_set_error_handler(codec, ErrorCallback, &log);

  opj_dparameters_t params;
  opj_set_default_decoder_parameters(&params);

  if (opj_setup_decoder(codec, &params) != OPJ_TRUE) {
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: failed to setup decoder");
  }

  opj_image_t* image = nullptr;
  if (opj_read_header(stream, codec, &image) != OPJ_TRUE || image == nullptr) {
    const std::string detail =
        log.first_error.empty() ? "" : (": " + log.first_error);
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: failed to read header" + detail);
  }

  if (opj_decode(codec, stream, image) != OPJ_TRUE) {
    const std::string detail =
        log.first_error.empty() ? "" : (": " + log.first_error);
    opj_image_destroy(image);
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: decode failed" + detail);
  }

  (void)opj_end_decompress(codec, stream);
  opj_destroy_codec(codec);
  opj_stream_destroy(stream);
  return image;
}

Result<void> PackInterleaved(const opj_image_t& image, bool file_big_endian,
                             uint16_t expected_bits_per_sample,
                             uint16_t expected_samples_per_pixel,
                             bool convert_ycbcr_to_rgb, int& out_width,
                             int& out_height, std::vector<uint8_t>& out) {
  const uint32_t width = image.x1 - image.x0;
  const uint32_t height = image.y1 - image.y0;
  if (width == 0 || height == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: decoded image has zero size");
  }

  if (image.numcomps == 0 || image.comps == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: decoded image has no components");
  }

  if (expected_samples_per_pixel == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid SamplesPerPixel=0");
  }

  if (expected_bits_per_sample != 8 && expected_bits_per_sample != 16 &&
      expected_bits_per_sample != 32) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                                "Unsupported BitsPerSample for JPEG2000: " +
                                    std::to_string(expected_bits_per_sample));
  }

  // Validate components. OpenJPEG can return subsampled component planes
  // (dx/dy > 1). SimpleTIFF supports these via nearest-neighbor upsampling
  // while interleaving (and/or converting YCbCr->RGB).
  for (uint32_t c = 0; c < image.numcomps; ++c) {
    const auto& comp = image.comps[c];
    if (comp.dx == 0 || comp.dy == 0) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          "OpenJPEG: invalid component sampling (dx/dy=0)");
    }
    const uint32_t expected_w = (width + comp.dx - 1u) / comp.dx;
    const uint32_t expected_h = (height + comp.dy - 1u) / comp.dy;
    if (comp.w != expected_w || comp.h != expected_h) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          "OpenJPEG: component dimensions mismatch (unexpected subsampling)");
    }
    if (comp.sgnd != 0) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          "OpenJPEG: signed components are not supported");
    }
    if (comp.data == nullptr) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "OpenJPEG: missing component data");
    }
  }

  const bool want_rgb = convert_ycbcr_to_rgb;
  const uint16_t out_samples =
      want_rgb ? static_cast<uint16_t>(3) : expected_samples_per_pixel;

  if (!want_rgb) {
    if (image.numcomps != expected_samples_per_pixel) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          "OpenJPEG: component count " + std::to_string(image.numcomps) +
              " does not match TIFF SamplesPerPixel=" +
              std::to_string(expected_samples_per_pixel));
    }
  } else {
    if (image.numcomps < 3) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          "OpenJPEG: YCbCr->RGB requested but image has <3 components");
    }
  }

  // Validate precision against TIFF BitsPerSample (we accept <= for safety, but
  // reject clearly incompatible images).
  for (uint32_t c = 0; c < (want_rgb ? 3u : image.numcomps); ++c) {
    const auto& comp = image.comps[c];
    if (comp.prec == 0 || comp.prec > 32) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                                  "OpenJPEG: unsupported component precision");
    }
    if (comp.prec > expected_bits_per_sample) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "OpenJPEG: component precision (" +
                                      std::to_string(comp.prec) +
                                      " bits) exceeds TIFF BitsPerSample=" +
                                      std::to_string(expected_bits_per_sample));
    }
  }

  const size_t pixels =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  const size_t bytes_per_sample = expected_bits_per_sample / 8;
  const size_t bytes_per_pixel = bytes_per_sample * out_samples;

  out.resize(pixels * bytes_per_pixel);
  out_width = static_cast<int>(width);
  out_height = static_cast<int>(height);

  auto write_sample = [&](size_t dst_offset, uint32_t v) {
    if (expected_bits_per_sample == 8) {
      out[dst_offset] = static_cast<uint8_t>(ClampTo<uint32_t>(v, 0u, 255u));
      return;
    }
    if (expected_bits_per_sample == 16) {
      const uint16_t vv =
          static_cast<uint16_t>(ClampTo<uint32_t>(v, 0u, 65535u));
      if (file_big_endian) {
        out[dst_offset + 0] = static_cast<uint8_t>((vv >> 8) & 0xFF);
        out[dst_offset + 1] = static_cast<uint8_t>(vv & 0xFF);
      } else {
        out[dst_offset + 0] = static_cast<uint8_t>(vv & 0xFF);
        out[dst_offset + 1] = static_cast<uint8_t>((vv >> 8) & 0xFF);
      }
      return;
    }
    // 32-bit
    const uint32_t vv = v;
    if (file_big_endian) {
      out[dst_offset + 0] = static_cast<uint8_t>((vv >> 24) & 0xFF);
      out[dst_offset + 1] = static_cast<uint8_t>((vv >> 16) & 0xFF);
      out[dst_offset + 2] = static_cast<uint8_t>((vv >> 8) & 0xFF);
      out[dst_offset + 3] = static_cast<uint8_t>(vv & 0xFF);
    } else {
      out[dst_offset + 0] = static_cast<uint8_t>(vv & 0xFF);
      out[dst_offset + 1] = static_cast<uint8_t>((vv >> 8) & 0xFF);
      out[dst_offset + 2] = static_cast<uint8_t>((vv >> 16) & 0xFF);
      out[dst_offset + 3] = static_cast<uint8_t>((vv >> 24) & 0xFF);
    }
  };

  if (!want_rgb) {
    // Port of OpenJPEG's component upsampling semantics from
    // opj_decompress.c:upsample_image_components(). This differs from a naïve
    // clamp-based nearest-neighbor sampler because it accounts for component
    // x0/y0 alignment offsets and fills "outside" regions with 0.
    const auto sample_comp = [&](uint32_t comp_index, uint32_t x,
                                 uint32_t y) -> uint32_t {
      const auto& comp = image.comps[comp_index];
      const int64_t xoff64 =
          static_cast<int64_t>(comp.dx) * static_cast<int64_t>(comp.x0) -
          static_cast<int64_t>(image.x0);
      const int64_t yoff64 =
          static_cast<int64_t>(comp.dy) * static_cast<int64_t>(comp.y0) -
          static_cast<int64_t>(image.y0);
      if (xoff64 < 0 || yoff64 < 0) {
        return 0;
      }
      const uint32_t xoff = static_cast<uint32_t>(xoff64);
      const uint32_t yoff = static_cast<uint32_t>(yoff64);
      if (x < xoff || y < yoff) {
        return 0;
      }
      const uint32_t xr = x - xoff;
      const uint32_t yr = y - yoff;
      const uint32_t sx = xr / comp.dx;
      const uint32_t sy = yr / comp.dy;
      if (sx >= comp.w || sy >= comp.h) {
        return 0;
      }
      const size_t idx = static_cast<size_t>(sy) * static_cast<size_t>(comp.w) +
                         static_cast<size_t>(sx);
      return static_cast<uint32_t>(comp.data[idx]);
    };

    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const size_t i =
            static_cast<size_t>(y) * static_cast<size_t>(width) + x;
        const size_t base = i * bytes_per_pixel;
        for (uint16_t c = 0; c < out_samples; ++c) {
          const uint32_t v = sample_comp(static_cast<uint32_t>(c), x, y);
          write_sample(base + static_cast<size_t>(c) * bytes_per_sample, v);
        }
      }
    }
    return Result<void>();
  }

  // Port of OpenJPEG 2.4.0 sYCC->RGB conversion (src/bin/common/color.c):
  // - Handles 4:4:4, 4:2:2 and 4:2:0 sampling patterns.
  // - Uses the same coefficients (0.344/0.714) and truncation behavior.
  const auto write_rgb = [&](size_t pixel_index, uint32_t r, uint32_t g,
                             uint32_t b) {
    const size_t base = pixel_index * bytes_per_pixel;
    write_sample(base + 0 * bytes_per_sample, r);
    write_sample(base + 1 * bytes_per_sample, g);
    write_sample(base + 2 * bytes_per_sample, b);
  };

  const uint32_t prec = image.comps[0].prec;
  if (prec == 0 || prec > 32) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        "OpenJPEG: unsupported component precision for SYCC conversion");
  }
  const uint32_t offset = (prec == 32) ? (1u << 31) : (1u << (prec - 1u));
  const uint32_t upb =
      (prec == 32) ? std::numeric_limits<uint32_t>::max() : ((1u << prec) - 1u);

  const auto sycc_to_rgb = [&](int y, int cb, int cr, uint32_t& out_r,
                               uint32_t& out_g, uint32_t& out_b) {
    // This is intentionally close to OpenJPEG's implementation:
    // cb -= offset; cr -= offset; and float-multiply + truncation.
    const int cb_off = cb - static_cast<int>(offset);
    const int cr_off = cr - static_cast<int>(offset);

    const int r_add = static_cast<int>(1.402f * static_cast<float>(cr_off));
    const int g_sub = static_cast<int>(0.344f * static_cast<float>(cb_off) +
                                       0.714f * static_cast<float>(cr_off));
    const int b_add = static_cast<int>(1.772f * static_cast<float>(cb_off));

    const int64_t r = static_cast<int64_t>(y) + static_cast<int64_t>(r_add);
    const int64_t g = static_cast<int64_t>(y) - static_cast<int64_t>(g_sub);
    const int64_t b = static_cast<int64_t>(y) + static_cast<int64_t>(b_add);

    const auto clamp = [&](int64_t v) -> uint32_t {
      if (v < 0) {
        return 0;
      }
      if (v > static_cast<int64_t>(upb)) {
        return upb;
      }
      return static_cast<uint32_t>(v);
    };

    out_r = clamp(r);
    out_g = clamp(g);
    out_b = clamp(b);
  };

  const auto* y = image.comps[0].data;
  const auto* cb = image.comps[1].data;
  const auto* cr = image.comps[2].data;
  if (y == nullptr || cb == nullptr || cr == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "OpenJPEG: missing component data");
  }

  const size_t maxw = static_cast<size_t>(image.comps[0].w);
  const size_t maxh = static_cast<size_t>(image.comps[0].h);
  const auto* y0 = y;

  const bool is_420 = (image.comps[0].dx == 1) && (image.comps[1].dx == 2) &&
                      (image.comps[2].dx == 2) && (image.comps[0].dy == 1) &&
                      (image.comps[1].dy == 2) && (image.comps[2].dy == 2);
  const bool is_422 = (image.comps[0].dx == 1) && (image.comps[1].dx == 2) &&
                      (image.comps[2].dx == 2) && (image.comps[0].dy == 1) &&
                      (image.comps[1].dy == 1) && (image.comps[2].dy == 1);
  const bool is_444 = (image.comps[0].dx == 1) && (image.comps[1].dx == 1) &&
                      (image.comps[2].dx == 1) && (image.comps[0].dy == 1) &&
                      (image.comps[1].dy == 1) && (image.comps[2].dy == 1);

  if (is_444) {
    for (size_t i = 0; i < pixels; ++i) {
      uint32_t rr = 0, gg = 0, bb = 0;
      sycc_to_rgb(y[i], cb[i], cr[i], rr, gg, bb);
      write_rgb(i, rr, gg, bb);
    }
    return Result<void>();
  }

  if (is_422) {
    const size_t offx = static_cast<size_t>(image.x0) & 1u;
    const size_t loopmaxw = maxw - offx;

    for (size_t i = 0; i < maxh; ++i) {
      size_t j = 0;

      if (offx > 0u) {
        const size_t idx = static_cast<size_t>(y - y0);
        uint32_t rr = 0, gg = 0, bb = 0;
        sycc_to_rgb(*y, 0, 0, rr, gg, bb);
        write_rgb(idx, rr, gg, bb);
        ++y;
      }

      for (j = 0; j < (loopmaxw & ~(size_t)1u); j += 2u) {
        {
          const size_t idx = static_cast<size_t>(y - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*y, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++y;
        }
        {
          const size_t idx = static_cast<size_t>(y - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*y, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++y;
        }
        ++cb;
        ++cr;
      }

      if (j < loopmaxw) {
        const size_t idx = static_cast<size_t>(y - y0);
        uint32_t rr = 0, gg = 0, bb = 0;
        sycc_to_rgb(*y, *cb, *cr, rr, gg, bb);
        write_rgb(idx, rr, gg, bb);
        ++y;
        ++cb;
        ++cr;
      }
    }
    return Result<void>();
  }

  if (is_420) {
    const size_t offx = static_cast<size_t>(image.x0) & 1u;
    const size_t loopmaxw = maxw - offx;
    const size_t offy = static_cast<size_t>(image.y0) & 1u;
    const size_t loopmaxh = maxh - offy;

    if (offy > 0u) {
      for (size_t j = 0; j < maxw; ++j) {
        const size_t idx = static_cast<size_t>(y - y0);
        uint32_t rr = 0, gg = 0, bb = 0;
        sycc_to_rgb(*y, 0, 0, rr, gg, bb);
        write_rgb(idx, rr, gg, bb);
        ++y;
      }
    }

    size_t i = 0;
    for (i = 0u; i < (loopmaxh & ~(size_t)1u); i += 2u) {
      size_t j = 0;
      const auto* ny = y + maxw;

      if (offx > 0u) {
        {
          const size_t idx = static_cast<size_t>(y - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*y, 0, 0, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++y;
        }
        {
          const size_t idx = static_cast<size_t>(ny - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*ny, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++ny;
        }
      }

      for (j = 0u; j < (loopmaxw & ~(size_t)1u); j += 2u) {
        for (int k = 0; k < 2; ++k) {
          const size_t idx = static_cast<size_t>(y - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*y, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++y;
        }
        for (int k = 0; k < 2; ++k) {
          const size_t idx = static_cast<size_t>(ny - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*ny, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++ny;
        }
        ++cb;
        ++cr;
      }

      if (j < loopmaxw) {
        {
          const size_t idx = static_cast<size_t>(y - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*y, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++y;
        }
        {
          const size_t idx = static_cast<size_t>(ny - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*ny, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++ny;
        }
        ++cb;
        ++cr;
      }

      // Match OpenJPEG pointer arithmetic: y ends up at the start of the next
      // row after ny; then skip one more row to move to the next 2-row block.
      y += maxw;
    }

    if (i < loopmaxh) {
      size_t j = 0;
      for (j = 0u; j < (maxw & ~(size_t)1u); j += 2u) {
        for (int k = 0; k < 2; ++k) {
          const size_t idx = static_cast<size_t>(y - y0);
          uint32_t rr = 0, gg = 0, bb = 0;
          sycc_to_rgb(*y, *cb, *cr, rr, gg, bb);
          write_rgb(idx, rr, gg, bb);
          ++y;
        }
        ++cb;
        ++cr;
      }
      if (j < maxw) {
        const size_t idx = static_cast<size_t>(y - y0);
        uint32_t rr = 0, gg = 0, bb = 0;
        sycc_to_rgb(*y, *cb, *cr, rr, gg, bb);
        write_rgb(idx, rr, gg, bb);
      }
    }

    return Result<void>();
  }

  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kUnimplemented,
      "OpenJPEG: SYCC subsampling pattern is not supported");
}

}  // namespace

// Test-only entry point (used by //aifo/simpletiff:jpeg2000_sycc_pack_test).
// This intentionally lives in the production translation unit so we can keep
// the packer implementation private while still regression-testing subtle edge
// behavior.
Result<void> PackInterleavedForTest(const opj_image_t& image,
                                    bool file_big_endian,
                                    uint16_t expected_bits_per_sample,
                                    uint16_t expected_samples_per_pixel,
                                    bool convert_ycbcr_to_rgb, int& out_width,
                                    int& out_height,
                                    std::vector<uint8_t>& out) {
  return PackInterleaved(image, file_big_endian, expected_bits_per_sample,
                         expected_samples_per_pixel, convert_ycbcr_to_rgb,
                         out_width, out_height, out);
}

Result<void> DecodeJpeg2000(std::span<const uint8_t> compressed,
                            bool file_big_endian,
                            uint16_t expected_bits_per_sample,
                            uint16_t expected_samples_per_pixel,
                            bool convert_ycbcr_to_rgb, int& out_width,
                            int& out_height, std::vector<uint8_t>& out) {
  if (compressed.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "JPEG2000 buffer is empty");
  }
  // Fail fast on obviously invalid inputs: even the smallest valid J2K/JP2
  // bitstreams are larger than just a couple of bytes. This also protects
  // against pathological behavior in downstream decoders on truncated inputs.
  if (compressed.size() < 8) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "JPEG2000 buffer is too small");
  }

  const OPJ_CODEC_FORMAT format =
      IsJp2(compressed) ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K;
  if (!IsJp2(compressed) && !IsJ2kCodestream(compressed)) {
    // Still try OPJ_CODEC_J2K (some writers omit the SOC marker in odd ways),
    // but provide a helpful hint in the error message if it fails.
  }

  AIFOCORE_ASSIGN_OR_RETURN(opj_image_t * image,
                            DecodeOpenJpeg(compressed, format));
  auto cleanup = [&]() {
    opj_image_destroy(image);
  };

  Result<void> pack_result =
      PackInterleaved(*image, file_big_endian, expected_bits_per_sample,
                      expected_samples_per_pixel, convert_ycbcr_to_rgb,
                      out_width, out_height, out);
  cleanup();
  return pack_result;
}

#endif  // SIMPLETIFF_HAS_OPENJPEG

}  // namespace simpletiff
