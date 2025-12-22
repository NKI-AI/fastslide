//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"

typedef struct isyntax_t isyntax_t;
typedef struct isyntax_cache_t isyntax_cache_t;
typedef struct isyntax_image_t isyntax_image_t;
typedef struct isyntax_level_t isyntax_level_t;

namespace isyntax {

enum class PixelFormat : int32_t {
  kRgba = 1,
  kBgra = 2,
};

struct RgbaImage {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<uint32_t> pixels;  // size = width * height
};

struct JpegBuffer {
  std::vector<uint8_t> bytes;
};

struct IccProfile {
  std::vector<uint8_t> bytes;
};

class IsyntaxCache;

/// C++ entry point for opening and reading Philips iSyntax files.
///
/// This is intended to replace the legacy C API
/// (`libisyntax.h`/`libisyntax.c`). The implementation avoids exceptions and
/// uses `aifocore::Status/Result`.
class IsyntaxFile {
 public:
  static aifocore::Result<std::unique_ptr<IsyntaxFile>> Open(
      std::string_view filename, bool dump_xml_header = false);

  IsyntaxFile(const IsyntaxFile&) = delete;
  IsyntaxFile& operator=(const IsyntaxFile&) = delete;
  IsyntaxFile(IsyntaxFile&&) = delete;
  IsyntaxFile& operator=(IsyntaxFile&&) = delete;

  ~IsyntaxFile();

  /// Ensures thread-local memory is initialized for the calling thread.
  static void EnsureThreadInit();

  /// Read a tile into a caller-provided buffer.
  aifocore::Status ReadTile(int32_t level, int64_t tile_x, int64_t tile_y,
                            std::span<uint32_t> out_pixels,
                            PixelFormat pixel_format) const;

  int32_t tile_width() const;
  int32_t tile_height() const;

  int32_t level_count() const;
  const isyntax_level_t* level(int32_t idx) const;

  const char* barcode() const;

  aifocore::Result<RgbaImage> ReadLabelImage(PixelFormat pixel_format) const;
  aifocore::Result<RgbaImage> ReadMacroImage(PixelFormat pixel_format) const;

  aifocore::Result<JpegBuffer> ReadLabelImageJpeg() const;
  aifocore::Result<JpegBuffer> ReadMacroImageJpeg() const;

  aifocore::Result<IccProfile> ReadIccProfileForWsi() const;

  /// Access the internal cache object (owned by IsyntaxFile).
  isyntax_cache_t* cache() const;

  /// Returns the underlying internal handle (borrowed).
  isyntax_t* handle() const { return isyntax_.get(); }

 private:
  struct IsyntaxDeleter {
    void operator()(isyntax_t* p) const;
  };

  explicit IsyntaxFile(std::unique_ptr<isyntax_t, IsyntaxDeleter> isyntax,
                       std::unique_ptr<IsyntaxCache> cache);

  std::unique_ptr<isyntax_t, IsyntaxDeleter> isyntax_;
  std::unique_ptr<IsyntaxCache> cache_;
};

}  // namespace isyntax
