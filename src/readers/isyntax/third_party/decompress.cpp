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

#include "fastslide/readers/isyntax/third_party/decompress.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/platform/intrinsics.h"
#include "fastslide/readers/isyntax/third_party/utils/arena_allocator.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace isyntax {
namespace {

// Lookup table for (1 << n) - 1
constexpr std::array<uint16_t, 17> kSizeBitmasks = {
    0,   1,    3,    7,    15,   31,    63,    127,  255,
    511, 1023, 2047, 4095, 8191, 16383, 32767, 65535};

constexpr int32_t kHuffmanFastBits = 11;
constexpr uint32_t kFastMask = (1 << kHuffmanFastBits) - 1;

/// @brief Read between 57 and 64 bits from a bitstream (LSB first)
inline uint64_t BitstreamLsbRead(uint8_t* buffer, uint32_t pos) {
  uint64_t raw = *reinterpret_cast<uint64_t*>(buffer + pos / 8);
  raw >>= pos % 8;
  return raw;
}

/// @brief Convert signed magnitude block to two's complement using SIMD
void SignedMagnitudeToTwosComplement16Block(uint16_t* data, uint32_t len) {
  uint32_t aligned_len = (len / 8) * 8;
  uint32_t i = 0;

#if defined(__SSE2__)
  // Fast x86 SIMD version
  for (; i < aligned_len; i += 8) {
    __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    __m128i sign_masks = _mm_srai_epi16(x, 15);
    __m128i maybe_positive = _mm_andnot_si128(sign_masks, x);
    __m128i value_if_negative =
        _mm_sub_epi16(_mm_and_si128(x, _mm_set1_epi16(0x8000)), x);
    __m128i maybe_negative = _mm_and_si128(sign_masks, value_if_negative);
    __m128i result = _mm_or_si128(maybe_positive, maybe_negative);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(data + i), result);
  }
#elif defined(__ARM_NEON)
  // NEON version for ARM processors
  for (; i < aligned_len; i += 8) {
    uint16x8_t x = vld1q_u16(data + i);
    int16x8_t sign_masks = vshrq_n_s16(reinterpret_cast<int16x8_t>(x), 15);
    uint16x8_t maybe_positive =
        vbicq_u16(x, reinterpret_cast<uint16x8_t>(sign_masks));
    uint16x8_t value_if_negative =
        vsubq_u16(vandq_u16(x, vdupq_n_u16(0x8000)), x);
    uint16x8_t maybe_negative =
        vandq_u16(reinterpret_cast<uint16x8_t>(sign_masks), value_if_negative);
    uint16x8_t result = vorrq_u16(maybe_positive, maybe_negative);
    vst1q_u16(data + i, result);
  }
#endif

  // Scalar version for remaining elements
  for (; i < len; ++i) {
    data[i] = SignedMagnitudeToTwosComplement16(data[i]);
  }
}

void SaveCodeInHuffmanFastLookupTable(HuffmanTable* huffman, uint32_t code,
                                      uint32_t code_width, uint8_t symbol) {
  ASSERT(code_width <= kHuffmanFastBits);
  int32_t duplicate_bits = kHuffmanFastBits - code_width;
  for (uint32_t i = 0; i < (1u << duplicate_bits); ++i) {
    uint32_t address = (i << code_width) | code;
    huffman->fast[address] = symbol;
  }
}

}  // namespace

// HulskenDecompressor implementation

aifocore::Status HulskenDecompressor::ParseCodeblockHeader(int32_t* bits_read) {
  coeff_count_ = (coefficient_ == 1) ? 3 : 1;
  coeff_bit_depth_ = 16;  // fixed value for iSyntax
  coeff_buffer_size_ =
      coeff_count_ * block_width_ * block_height_ * sizeof(int16_t);
  block_size_in_bits_ = compressed_size_ * 8;

  // Set default bitmasks (all ones for v1)
  bitmasks_[0] = 0x000FFFF;
  bitmasks_[1] = 0x000FFFF;
  bitmasks_[2] = 0x000FFFF;
  total_mask_bits_ = coeff_bit_depth_ * coeff_count_;

  uint8_t* byte_pos = compressed_;
  *bits_read = 0;

  // Parse version-specific header
  if (compressor_version_ == 1) {
    serialized_length_ = *reinterpret_cast<uint32_t*>(byte_pos);
    byte_pos += 4;
    *bits_read += 4 * 8;
  } else {
    if (coeff_count_ == 1) {
      bitmasks_[0] = *reinterpret_cast<uint16_t*>(byte_pos);
      byte_pos += 2;
      *bits_read += 2 * 8;
      total_mask_bits_ = popcount(bitmasks_[0]);
    } else if (coeff_count_ == 3) {
      bitmasks_[0] = *reinterpret_cast<uint16_t*>(byte_pos);
      bitmasks_[1] = *reinterpret_cast<uint16_t*>(byte_pos + 2);
      bitmasks_[2] = *reinterpret_cast<uint16_t*>(byte_pos + 4);
      byte_pos += 6;
      *bits_read += 6 * 8;
      total_mask_bits_ = popcount(bitmasks_[0]) + popcount(bitmasks_[1]) +
                         popcount(bitmasks_[2]);
    } else {
      return aifocore::Status(aifocore::StatusCode::kInternal,
                              "Invalid coeff_count in decompression");
    }
    serialized_length_ = total_mask_bits_ * (block_width_ * block_height_ / 8);
  }

  // Validate serialized length
  if (serialized_length_ > 2 * static_cast<int64_t>(coeff_buffer_size_)) {
    return aifocore::Status(
        aifocore::StatusCode::kDataLoss,
        aifocore::fmt::format(
            "Invalid codeblock: serialized_length too large ({})",
            serialized_length_));
  }

  // Read zero run encoding parameters
  zerorun_symbol_ = *byte_pos++;
  *bits_read += 8;
  zero_counter_size_ = *byte_pos++;
  *bits_read += 8;

  // Read bitplane seektable (version 2 only)
  bitplane_offsets_.fill(0);
  if (compressor_version_ >= 2) {
    uint32_t bitmasks_aggregate = 0;
    for (int32_t i = 0; i < coeff_count_; ++i) {
      bitmasks_aggregate |= bitmasks_[i];
    }
    int32_t bitplane_ptr_count = popcount(bitmasks_aggregate);
    int32_t bitplane_ptr_bits =
        static_cast<int32_t>(std::log2f(serialized_length_)) + 5;
    uint32_t bitplane_ptr_mask = (1u << bitplane_ptr_bits) - 1;
    for (int32_t i = 0; i < bitplane_ptr_count - 1; ++i) {
      uint64_t blob = BitstreamLsbRead(compressed_, *bits_read);
      bitplane_offsets_[i] = blob & bitplane_ptr_mask;
      *bits_read += bitplane_ptr_bits;
    }
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status HulskenDecompressor::BuildHuffmanTable(HuffmanTable* huffman,
                                                        int32_t* bits_read) {
  std::memset(huffman->fast.data(), 0x80,
              huffman->fast.size() * sizeof(uint16_t));
  std::memset(huffman->nonfast_size_masks.data(), 0xFF,
              huffman->nonfast_size_masks.size() * sizeof(uint16_t));

  int32_t code_size = 0;
  uint32_t code = 0;
  int32_t nonfast_symbol_index = 0;

  do {
    if (*bits_read >= block_size_in_bits_) {
      return aifocore::Status(
          aifocore::StatusCode::kDataLoss,
          aifocore::fmt::format("Invalid codeblock: Huffman table extends out "
                                "of bounds (size={})",
                                compressed_size_));
    }

    // Read Huffman code and symbol
    int32_t bits_to_advance = 1;
    uint64_t blob = BitstreamLsbRead(compressed_, *bits_read);

    // Descend the tree until we hit a leaf node
    bool is_leaf = blob & 1;
    while (!is_leaf) {
      ++bits_to_advance;
      blob >>= 1;
      is_leaf = (blob & 1);
      ++code_size;
    }
    blob >>= 1;

    // Read 8-bit Huffman symbol
    uint8_t symbol = static_cast<uint8_t>(blob);
    huffman->code[symbol] = code;
    huffman->size[symbol] = code_size;

    if (code_size <= kHuffmanFastBits) {
      // Fast path: store in lookup table
      SaveCodeInHuffmanFastLookupTable(huffman, code, code_size, symbol);
    } else {
      // Slow path for long codes
      uint32_t prefix = code & kFastMask;
      uint16_t old_fast_data = huffman->fast[prefix];
      uint8_t old_lowest_symbol_index = old_fast_data & 0xFF;
      uint8_t new_lowest_symbol_index = std::min(
          old_lowest_symbol_index, static_cast<uint8_t>(nonfast_symbol_index));
      huffman->fast[prefix] = 256 + new_lowest_symbol_index;
      huffman->nonfast_symbols[nonfast_symbol_index] = symbol;
      huffman->nonfast_code[nonfast_symbol_index] = code;
      huffman->nonfast_size[nonfast_symbol_index] = code_size;
      huffman->nonfast_size_masks[nonfast_symbol_index] =
          kSizeBitmasks[code_size];
      ++nonfast_symbol_index;
    }

    bits_to_advance += 8;
    *bits_read += bits_to_advance;

    // Traverse back up the tree
    if (code_size == 0) {
      break;  // root node only
    }
    uint32_t code_high_bit = (1 << (code_size - 1));
    bool found_zero = (~code) & code_high_bit;
    while (!found_zero) {
      --code_size;
      if (code_size == 0)
        break;
      code &= code_high_bit - 1;
      code_high_bit >>= 1;
      found_zero = (~code) & code_high_bit;
    }
    code |= code_high_bit;
  } while (code_size > 0);

  return aifocore::Status::OkStatus();
}

aifocore::Status HulskenDecompressor::DecodeHuffmanMessage(
    const HuffmanTable& huffman, int32_t* bits_read,
    ScopedArenaMemory* temp_memory, uint8_t** decompressed_buffer,
    int32_t* decompressed_length) {

  *decompressed_buffer =
      temp_memory->AllocateArray<uint8_t>(serialized_length_);

  uint32_t zerorun_code = huffman.code[zerorun_symbol_];
  uint32_t zerorun_code_size = huffman.size[zerorun_symbol_];
  if (zerorun_code_size == 0)
    zerorun_code_size = 1;  // handle empty Huffman tree
  uint32_t zerorun_code_mask = (1 << zerorun_code_size) - 1;
  uint32_t zero_counter_mask = (1 << zero_counter_size_) - 1;

  *decompressed_length = 0;
  while (*bits_read < block_size_in_bits_) {
    if (*decompressed_length >= serialized_length_ ||
        *bits_read >= block_size_in_bits_) {
      break;  // done
    }

    // Decode one Huffman symbol
    int32_t symbol = 0;
    int32_t code_size = 1;
    uint64_t blob = BitstreamLsbRead(compressed_, *bits_read);
    uint32_t fast_index = blob & kFastMask;
    uint16_t c = huffman.fast[fast_index];

    if (c <= 255) {
      // Fast path
      symbol = c;
      code_size = huffman.size[symbol];
    } else {
      // Slow path: search non-fast symbols
      bool match = false;
      uint8_t lowest_possible_symbol_index = c & 0xFF;

#if !((defined(__SSE2__) && defined(__AVX__)))
      // Scalar version
      for (int32_t i = lowest_possible_symbol_index; i < 256; ++i) {
        uint8_t test_size = huffman.nonfast_size[i];
        uint16_t test_code = huffman.nonfast_code[i];
        if ((blob & kSizeBitmasks[test_size]) == test_code) {
          code_size = test_size;
          symbol = huffman.nonfast_symbols[i];
          match = true;
          break;
        }
      }
#else
      // SIMD version using SSE2
      for (int32_t i = lowest_possible_symbol_index; i < 256; i += 8) {
        __m128i size_mask = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(&huffman.nonfast_size_masks[i]));
        __m128i code = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(&huffman.nonfast_code[i]));
        __m128i test = _mm_set1_epi16(static_cast<uint16_t>(blob));
        test = _mm_and_si128(test, size_mask);
        __m128i hit = _mm_cmpeq_epi16(test, code);
        uint32_t hit_mask = _mm_movemask_epi8(hit);
        if (hit_mask) {
          uint32_t first_bit = bit_scan_forward(hit_mask);
          int32_t symbol_index = i + first_bit / 2;
          symbol = huffman.nonfast_symbols[symbol_index];
          code_size = huffman.nonfast_size[symbol_index];
          match = true;
          break;
        }
      }
#endif
      if (!match) {
        return aifocore::Status(
            aifocore::StatusCode::kDataLoss,
            "Error decoding Huffman message: unknown symbol");
      }
    }

    if (code_size == 0)
      code_size = 1;  // handle empty Huffman tree

    blob >>= code_size;
    *bits_read += code_size;

    // Handle run-length encoding of zeroes
    if (symbol == zerorun_symbol_) {
      uint32_t numzeroes = blob & zero_counter_mask;
      *bits_read += zero_counter_size_;

      if (numzeroes > 0) {
        // Actual zero run
        uint32_t actual_numzeroes =
            (compressor_version_ == 2) ? numzeroes + 1 : numzeroes;

        if (*decompressed_length + actual_numzeroes >= serialized_length_ ||
            *bits_read >= block_size_in_bits_) {
          std::memset(*decompressed_buffer + *decompressed_length, 0,
                      std::min(serialized_length_ - *decompressed_length,
                               static_cast<int64_t>(actual_numzeroes)));
          *decompressed_length += actual_numzeroes;
          break;
        }

        // Check for extended zero runs
        for (;;) {
          blob = BitstreamLsbRead(compressed_, *bits_read);
          uint32_t next_code = (blob & zerorun_code_mask);
          if (next_code == zerorun_code) {
            blob >>= zerorun_code_size;
            uint32_t counter_extra_bits = blob & zero_counter_mask;
            numzeroes <<= zero_counter_size_;
            numzeroes |= counter_extra_bits;
            *bits_read += zerorun_code_size + zero_counter_size_;
            actual_numzeroes =
                (compressor_version_ == 2) ? numzeroes + 1 : numzeroes;
            if (*decompressed_length + actual_numzeroes >= serialized_length_ ||
                *bits_read >= block_size_in_bits_) {
              break;
            }
          } else {
            actual_numzeroes =
                (compressor_version_ == 2) ? numzeroes + 1 : numzeroes;
            break;
          }
        }

        int32_t bytes_to_write =
            std::min(serialized_length_ - *decompressed_length,
                     static_cast<int64_t>(actual_numzeroes));
        ASSERT(bytes_to_write > 0);
        std::memset(*decompressed_buffer + *decompressed_length, 0,
                    bytes_to_write);
        *decompressed_length += actual_numzeroes;
      } else {
        // Escaped symbol (zero run length of zero)
        (*decompressed_buffer)[(*decompressed_length)++] = symbol;
      }
    } else {
      // Normal symbol
      (*decompressed_buffer)[(*decompressed_length)++] = symbol;
    }
  }

  // Verify decompressed size
  if (serialized_length_ != *decompressed_length) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kDataLoss,
        aifocore::fmt::format(
            "iSyntax: decompressed size mismatch (compressed_size={}): "
            "expected {} observed {}",
            compressed_size_, serialized_length_, *decompressed_length));
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status HulskenDecompressor::PostprocessVersion1Bitmasks(
    uint8_t* decompressed_buffer, int32_t decompressed_length) {
  if (compressor_version_ != 1) {
    return aifocore::Status::OkStatus();
  }

  int32_t bytes_per_bitplane = (block_width_ * block_height_) / 8;

  // Try to deduce the number of coefficients
  int32_t extra_bits =
      (decompressed_length * 8) % (block_width_ * block_height_);
  if (extra_bits > 0) {
    if (coeff_count_ != 1 && extra_bits == 1 * 16) {
      coeff_count_ = 1;
    } else if (coeff_count_ != 3 && extra_bits == 3 * 16) {
      coeff_count_ = 3;
    }
    total_mask_bits_ = coeff_bit_depth_ * coeff_count_;
  }

  // If there are empty bitplanes: bitmasks stored at end of data
  uint64_t expected_length = total_mask_bits_ * bytes_per_bitplane;
  if (decompressed_length < expected_length) {
    if (coeff_count_ == 1) {
      bitmasks_[0] = *reinterpret_cast<uint16_t*>(decompressed_buffer +
                                                  decompressed_length - 2);
      total_mask_bits_ = popcount(bitmasks_[0]);
    } else if (coeff_count_ == 3) {
      uint8_t* byte_pos = decompressed_buffer + decompressed_length - 6;
      bitmasks_[0] = *reinterpret_cast<uint16_t*>(byte_pos);
      bitmasks_[1] = *reinterpret_cast<uint16_t*>(byte_pos + 2);
      bitmasks_[2] = *reinterpret_cast<uint16_t*>(byte_pos + 4);
      total_mask_bits_ = popcount(bitmasks_[0]) + popcount(bitmasks_[1]) +
                         popcount(bitmasks_[2]);
    } else {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format("Invalid coeff_count ({}): expected 1 or 3",
                                coeff_count_));
    }
    expected_length = (total_mask_bits_ * block_width_ * block_height_) / 8 +
                      (coeff_count_ * 2);
    ASSERT(decompressed_length == expected_length);
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status HulskenDecompressor::UnpackBitplanes(
    uint8_t* decompressed_buffer, uint16_t* coeff_buffer) {
  int32_t bytes_per_bitplane = (block_width_ * block_height_) / 8;
  uint32_t running_bit_index = 0;
  int32_t running_coeff_index = 0;
  std::array<uint32_t, 3> bitmasks_copy;
  std::memcpy(bitmasks_copy.data(), bitmasks_.data(), sizeof(bitmasks_));

  for (int32_t bitplane_index = 0; bitplane_index < total_mask_bits_;
       ++bitplane_index) {
    uint8_t* bitplane =
        decompressed_buffer + (bitplane_index * bytes_per_bitplane);

    // Determine which coefficient and bit this bitplane belongs to
    if (compressor_version_ == 1) {
      // Version 1: iterate over bitplanes for each coefficient separately
      for (;;) {
        if (running_coeff_index >= coeff_count_) {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kDataLoss,
              aifocore::fmt::format(
                  "iSyntax: too many bitplanes (v1) (coeff_count={})",
                  coeff_count_));
        }
        uint16_t bitmask = bitmasks_copy[running_coeff_index];
        if (bitmask) {
          running_bit_index = bit_scan_forward(bitmask);
          ASSERT(running_bit_index < 16);
          bitmasks_copy[running_coeff_index] &= ~(1 << running_bit_index);
          break;
        } else {
          ++running_coeff_index;
        }
      }
    } else {
      // Version 2: alternate coefficients (striped)
      for (;;) {
        if (running_bit_index >= 16) {
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kDataLoss,
              aifocore::fmt::format(
                  "iSyntax: too many bitplanes (v2) (coeff_count={})",
                  coeff_count_));
        }
        if (running_coeff_index < coeff_count_) {
          uint16_t bitmask = bitmasks_copy[running_coeff_index];
          if (bitmask & (1 << running_bit_index)) {
            bitmasks_copy[running_coeff_index] &= ~(1 << running_bit_index);
            break;
          } else {
            ++running_coeff_index;
          }
        } else {
          running_coeff_index = 0;
          ++running_bit_index;
        }
      }
    }

    uint16_t* current_coeff_buffer =
        coeff_buffer + (running_coeff_index * (block_width_ * block_height_));

    // Unpack this bitplane
    for (int32_t i = 0; i < block_width_ * block_height_; i += 8) {
      int32_t j = i / 8;
      int32_t shift_amount;
      if (compressor_version_ == 1) {
        shift_amount = (running_bit_index == 0) ? 15 : running_bit_index - 1;
      } else {
        shift_amount = 15 - running_bit_index;
      }
      uint8_t b = bitplane[j];
      if (b == 0)
        continue;

#if !defined(__SSE2__)
      // Scalar version
      current_coeff_buffer[i + 0] |= ((b >> 0) & 1) << shift_amount;
      current_coeff_buffer[i + 1] |= ((b >> 1) & 1) << shift_amount;
      current_coeff_buffer[i + 2] |= ((b >> 2) & 1) << shift_amount;
      current_coeff_buffer[i + 3] |= ((b >> 3) & 1) << shift_amount;
      current_coeff_buffer[i + 4] |= ((b >> 4) & 1) << shift_amount;
      current_coeff_buffer[i + 5] |= ((b >> 5) & 1) << shift_amount;
      current_coeff_buffer[i + 6] |= ((b >> 6) & 1) << shift_amount;
      current_coeff_buffer[i + 7] |= ((b >> 7) & 1) << shift_amount;
#else
      // SIMD implementation (~20% faster)
      __m128i* dst = reinterpret_cast<__m128i*>(current_coeff_buffer + i);
      uint64_t t =
          bswap_64(((0x8040201008040201ULL * b) & 0x8080808080808080ULL) >> 7);
      __m128i v_t = _mm_set_epi64x(0, t);
      __m128i array_of_bools = _mm_unpacklo_epi8(v_t, _mm_setzero_si128());
      __m128i masks = _mm_slli_epi16(array_of_bools, shift_amount);
      __m128i result = _mm_or_si128(*dst, masks);
      *dst = result;
#endif
    }

    // Update iteration state
    if (compressor_version_ == 2) {
      ++running_coeff_index;
    }
  }

  return aifocore::Status::OkStatus();
}

void HulskenDecompressor::ReshuffleAndConvert(uint16_t* coeff_buffer,
                                              int16_t* out_buffer) {
  for (int32_t coeff_index = 0; coeff_index < coeff_count_; ++coeff_index) {
    uint16_t bitmask = bitmasks_[coeff_index];
    uint16_t* current_coeff_buffer =
        coeff_buffer + (coeff_index * (block_width_ * block_height_));
    uint16_t* current_out_buffer =
        reinterpret_cast<uint16_t*>(out_buffer) +
        (coeff_index * (block_width_ * block_height_));

    if (bitmask > 0) {
      // Reshuffle from 4x2 snake-order to raster order
      int32_t area_stride_x = block_width_ / 4;
      for (int32_t area4x4_index = 0;
           area4x4_index < ((block_width_ * block_height_) / 16);
           ++area4x4_index) {
        int32_t area_base_index = area4x4_index * 16;
        int32_t area_x = (area4x4_index % area_stride_x) * 4;
        int32_t area_y = (area4x4_index / area_stride_x) * 4;

        uint64_t area_y0 = *reinterpret_cast<uint64_t*>(
            &current_coeff_buffer[area_base_index]);
        uint64_t area_y1 = *reinterpret_cast<uint64_t*>(
            &current_coeff_buffer[area_base_index + 4]);
        uint64_t area_y2 = *reinterpret_cast<uint64_t*>(
            &current_coeff_buffer[area_base_index + 8]);
        uint64_t area_y3 = *reinterpret_cast<uint64_t*>(
            &current_coeff_buffer[area_base_index + 12]);

        *reinterpret_cast<uint64_t*>(current_out_buffer +
                                     (area_y + 0) * block_width_ + area_x) =
            area_y0;
        *reinterpret_cast<uint64_t*>(current_out_buffer +
                                     (area_y + 1) * block_width_ + area_x) =
            area_y1;
        *reinterpret_cast<uint64_t*>(current_out_buffer +
                                     (area_y + 2) * block_width_ + area_x) =
            area_y2;
        *reinterpret_cast<uint64_t*>(current_out_buffer +
                                     (area_y + 3) * block_width_ + area_x) =
            area_y3;
      }

      // Convert signed magnitude to two's complement
      SignedMagnitudeToTwosComplement16Block(current_out_buffer,
                                             block_width_ * block_height_);
    }
  }
}

aifocore::Status HulskenDecompressor::Decompress(
    std::span<uint8_t> compressed, int32_t block_width, int32_t block_height,
    int32_t coefficient, int32_t compressor_version,
    std::span<int16_t> out_buffer) {
  if (compressor_version != 1 && compressor_version != 2) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid compressor version: must be 1 or 2");
  }

  // Initialize state
  compressed_ = compressed.data();
  compressed_size_ = compressed.size();
  block_width_ = block_width;
  block_height_ = block_height;
  coefficient_ = coefficient;
  compressor_version_ = compressor_version;

  coeff_count_ = (coefficient == 1) ? 3 : 1;
  coeff_bit_depth_ = 16;
  coeff_buffer_size_ =
      coeff_count_ * block_width * block_height * sizeof(int16_t);

  // Validate output buffer size
  size_t expected_out_size = coeff_count_ * block_width * block_height;
  if (out_buffer.size() < expected_out_size) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Output buffer too small: {} < {}",
                              out_buffer.size(), expected_out_size));
  }

  // Early out for empty blocks
  if (compressed.size() <= 8) {
    std::memset(out_buffer.data(), 0, coeff_buffer_size_);
    return aifocore::Status::OkStatus();
  }

  AIFOCORE_ASSIGN_OR_RETURN(ScopedArenaMemory temp_memory,
                            ThreadLocalArena::BeginScope());
  int32_t bits_read = 0;

  // Phase 1: Parse header
  AIFOCORE_RETURN_IF_ERROR(ParseCodeblockHeader(&bits_read));

  // Phase 2: Build Huffman table
  HuffmanTable huffman = {};
  AIFOCORE_RETURN_IF_ERROR(BuildHuffmanTable(&huffman, &bits_read));

  // Phase 3: Decode Huffman message
  uint8_t* decompressed_buffer = nullptr;
  int32_t decompressed_length = 0;
  AIFOCORE_RETURN_IF_ERROR(
      DecodeHuffmanMessage(huffman, &bits_read, &temp_memory,
                           &decompressed_buffer, &decompressed_length));

  // Phase 4: Post-process version 1 bitmasks
  AIFOCORE_RETURN_IF_ERROR(
      PostprocessVersion1Bitmasks(decompressed_buffer, decompressed_length));

  // Phase 5: Unpack bitplanes
  temp_memory.Align(32);
  uint16_t* coeff_buffer = temp_memory.AllocateArray<uint16_t>(
      coeff_buffer_size_ / sizeof(uint16_t));
  std::memset(coeff_buffer, 0, coeff_buffer_size_);
  std::memset(out_buffer.data(), 0, coeff_buffer_size_);

  AIFOCORE_RETURN_IF_ERROR(UnpackBitplanes(decompressed_buffer, coeff_buffer));

  // Phase 6: Reshuffle and convert
  ReshuffleAndConvert(coeff_buffer, out_buffer.data());

  return aifocore::Status::OkStatus();
}

}  // namespace isyntax

namespace isyntax {

aifocore::Status HulskenDecompress(std::span<uint8_t> compressed,
                                   int32_t block_width, int32_t block_height,
                                   int32_t coefficient,
                                   int32_t compressor_version,
                                   std::span<int16_t> out_buffer) {
  HulskenDecompressor decompressor;
  return decompressor.Decompress(compressed, block_width, block_height,
                                 coefficient, compressor_version, out_buffer);
}

}  // namespace isyntax
