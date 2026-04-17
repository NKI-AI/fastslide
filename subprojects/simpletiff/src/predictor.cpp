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

#include "simpletiff/predictor.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace simpletiff {

namespace {

// -----------------------------------------------------------
// Byte swap helpers
// -----------------------------------------------------------
static inline uint16_t Bswap16(uint16_t v) {
  return static_cast<uint16_t>((v << 8) | (v >> 8));
}

static inline uint32_t Bswap32(uint32_t v) {
  return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) |
         (v << 24);
}

template <class T>
inline T Byteswap(T v);

template <>
inline uint8_t Byteswap<uint8_t>(uint8_t v) {
  return v;
}

template <>
inline uint16_t Byteswap<uint16_t>(uint16_t v) {
  return Bswap16(v);
}

template <>
inline uint32_t Byteswap<uint32_t>(uint32_t v) {
  return Bswap32(v);
}

// -----------------------------------------------------------
// Undo predictor helpers
// -----------------------------------------------------------
template <class T>
inline void UndoPredictorRow(T* row, uint32_t width,
                             uint32_t samples_per_pixel) {
  if (width == 0)
    return;
  const uint32_t stride = samples_per_pixel;
  const uint32_t N = width * stride;
  for (uint32_t i = stride; i < N; ++i) {
    row[i] = static_cast<T>(row[i] + row[i - stride]);
  }
}

template <class T>
inline void UndoPredictorRowBytes(void* row_bytes, uint32_t width,
                                  uint32_t samples_per_pixel,
                                  bool file_big_endian) {
  T* row = reinterpret_cast<T*>(row_bytes);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (file_big_endian) {
    const uint32_t N = width * samples_per_pixel;
    for (uint32_t i = 0; i < N; ++i)
      row[i] = Byteswap<T>(row[i]);
  }
#endif

  UndoPredictorRow<T>(row, width, samples_per_pixel);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (file_big_endian) {
    const uint32_t N = width * samples_per_pixel;
    for (uint32_t i = 0; i < N; ++i)
      row[i] = Byteswap<T>(row[i]);
  }
#else
  (void)file_big_endian;
#endif
}

}  // namespace

// -----------------------------------------------------------
// Apply TIFF horizontal predictor (Predictor = 2)
// -----------------------------------------------------------
void ApplyHorizontalPredictor(std::vector<uint8_t>& data, int width, int height,
                              int samples_per_pixel, int bits_per_sample,
                              bool file_big_endian,
                              int planar_configuration /* = 1 */) {
  // Early validation - fail fast with clear error message
  if (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 32) {
    throw std::runtime_error(
        "Predictor=2: unsupported BitsPerSample=" +
        std::to_string(bits_per_sample) +
        ". SimpleTIFF requires byte-aligned formats (8, 16, or 32 bits).");
  }

  // CONTIG (1) = interleaved, SEPARATE (2) = planar
  const uint32_t bytes_per_sample = static_cast<uint32_t>(bits_per_sample / 8);
  uint8_t* p = data.data();

  if (planar_configuration == 1) {
    // Interleaved layout: RGBRGBRGB...
    // stride = samples_per_pixel (process all channels together)
    const uint32_t row_stride_bytes =
        static_cast<uint32_t>(width * samples_per_pixel * bytes_per_sample);
    for (int y = 0; y < height; ++y) {
      uint8_t* row_start = p + static_cast<size_t>(y) * row_stride_bytes;
      switch (bits_per_sample) {
        case 8:
          UndoPredictorRowBytes<uint8_t>(
              row_start, static_cast<uint32_t>(width),
              static_cast<uint32_t>(samples_per_pixel), file_big_endian);
          break;
        case 16:
          UndoPredictorRowBytes<uint16_t>(
              row_start, static_cast<uint32_t>(width),
              static_cast<uint32_t>(samples_per_pixel), file_big_endian);
          break;
        case 32:
          UndoPredictorRowBytes<uint32_t>(
              row_start, static_cast<uint32_t>(width),
              static_cast<uint32_t>(samples_per_pixel), file_big_endian);
          break;
        default:
          throw std::runtime_error("Predictor=2: unsupported BitsPerSample");
      }
    }
  } else if (planar_configuration == 2) {
    // Separate planes: RRR...GGG...BBB...
    // Each plane is processed independently with stride = 1
    const uint32_t plane_row_stride_bytes =
        static_cast<uint32_t>(width * bytes_per_sample);
    const uint32_t plane_size_bytes =
        plane_row_stride_bytes * static_cast<uint32_t>(height);

    for (int c = 0; c < samples_per_pixel; ++c) {
      uint8_t* plane_start = p + c * plane_size_bytes;
      for (int y = 0; y < height; ++y) {
        uint8_t* row_start =
            plane_start + static_cast<size_t>(y) * plane_row_stride_bytes;
        switch (bits_per_sample) {
          case 8:
            // stride = 1 for planar (single channel per plane)
            UndoPredictorRowBytes<uint8_t>(
                row_start, static_cast<uint32_t>(width), 1, file_big_endian);
            break;
          case 16:
            UndoPredictorRowBytes<uint16_t>(
                row_start, static_cast<uint32_t>(width), 1, file_big_endian);
            break;
          case 32:
            UndoPredictorRowBytes<uint32_t>(
                row_start, static_cast<uint32_t>(width), 1, file_big_endian);
            break;
          default:
            throw std::runtime_error("Predictor=2: unsupported BitsPerSample");
        }
      }
    }
  } else {
    throw std::runtime_error("Invalid PlanarConfiguration value");
  }
}

}  // namespace simpletiff
