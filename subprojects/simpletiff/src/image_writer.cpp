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

#include "simpletiff/image_writer.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef SIMPLETIFF_HAS_PNG
#include <png.h>
#endif

namespace simpletiff {

#ifdef SIMPLETIFF_HAS_PNG
bool WritePng(std::string_view filename, const uint8_t* rgb, int width,
              int height, int stride) {
  if (!rgb || width <= 0 || height <= 0) {
    return false;
  }

  FILE* fp = std::fopen(std::string(filename).c_str(), "wb");
  if (!fp) {
    return false;
  }

  png_structp png_ptr =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    std::fclose(fp);
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_write_struct(&png_ptr, nullptr);
    std::fclose(fp);
    return false;
  }

  // Set up error handling
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
    std::fclose(fp);
    return false;
  }

  png_init_io(png_ptr, fp);

  // Set image attributes
  png_set_IHDR(png_ptr, info_ptr, static_cast<png_uint_32>(width),
               static_cast<png_uint_32>(height), 8, PNG_COLOR_TYPE_RGB,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  // Write header
  png_write_info(png_ptr, info_ptr);

  // Write image data
  for (int y = 0; y < height; ++y) {
    png_write_row(png_ptr, rgb + y * stride);
  }

  // Finish writing
  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  std::fclose(fp);

  return true;
}

bool WritePngGrayscale(std::string_view filename, const uint8_t* gray,
                       int width, int height, int stride) {
  if (!gray || width <= 0 || height <= 0) {
    return false;
  }

  FILE* fp = std::fopen(std::string(filename).c_str(), "wb");
  if (!fp) {
    return false;
  }

  png_structp png_ptr =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    std::fclose(fp);
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_write_struct(&png_ptr, nullptr);
    std::fclose(fp);
    return false;
  }

  // Set up error handling
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
    std::fclose(fp);
    return false;
  }

  png_init_io(png_ptr, fp);

  // Set image attributes for grayscale
  png_set_IHDR(png_ptr, info_ptr, static_cast<png_uint_32>(width),
               static_cast<png_uint_32>(height), 8, PNG_COLOR_TYPE_GRAY,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  // Write header
  png_write_info(png_ptr, info_ptr);

  // Write image data
  for (int y = 0; y < height; ++y) {
    png_write_row(png_ptr, gray + y * stride);
  }

  // Finish writing
  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  std::fclose(fp);

  return true;
}
#else
bool WritePng(std::string_view, const uint8_t*, int, int, int) {
  // PNG support not compiled in
  return false;
}

bool WritePngGrayscale(std::string_view, const uint8_t*, int, int, int) {
  // PNG support not compiled in
  return false;
}
#endif

bool WritePpm(std::string_view filename, const uint8_t* rgb, int width,
              int height, int stride) {
  if (!rgb || width <= 0 || height <= 0) {
    return false;
  }

  FILE* fp = std::fopen(std::string(filename).c_str(), "wb");
  if (!fp) {
    return false;
  }

  // Write PPM header
  std::fprintf(fp, "P6\n%d %d\n255\n", width, height);

  // Write image data
  if (stride == width * 3) {
    // Contiguous data - write all at once
    std::fwrite(rgb, 1, static_cast<size_t>(width) * height * 3, fp);
  } else {
    // Non-contiguous - write row by row
    for (int y = 0; y < height; ++y) {
      std::fwrite(rgb + y * stride, 1, static_cast<size_t>(width) * 3, fp);
    }
  }

  std::fclose(fp);
  return true;
}

}  // namespace simpletiff
