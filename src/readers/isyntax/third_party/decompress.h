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

#include <array>
#include <cstdint>
#include <span>

#include "aifocore/status/result.h"
#include "readers/isyntax/third_party/platform/common.h"

namespace isyntax {

// Forward declarations
class ScopedArenaMemory;
class Arena;

/// @brief Convert signed magnitude representation to two's complement
///
/// This function is its own inverse (conversion works the other way as well).
/// For example: 0x8002 (signed magnitude -2) becomes 0xFFFE (two's complement
/// -2)
///
/// @param x Value in signed magnitude format
/// @return Value in two's complement format
constexpr int16_t SignedMagnitudeToTwosComplement16(uint16_t x) {
  uint16_t m = -(x >> 15);
  int16_t result = (~m & x) | (((x & static_cast<uint16_t>(0x8000)) - x) & m);
  return result;
}

/// @brief Huffman decoding table
struct HuffmanTable {
  std::array<uint16_t, 1 << 11> fast;          // Fast lookup table
  std::array<uint16_t, 256> code;              // Huffman code for each symbol
  std::array<uint8_t, 256> size;               // Code size for each symbol
  std::array<uint16_t, 256> nonfast_symbols;   // Symbols for slow path
  std::array<uint16_t, 256 + 7> nonfast_code;  // Codes for slow path
  std::array<uint16_t, 256> nonfast_size;      // Sizes for slow path
  std::array<uint16_t, 256 + 7> nonfast_size_masks;  // Bitmasks for slow path
};

/// @brief Hulsken decompression algorithm for iSyntax codeblocks
///
/// Implements the algorithm from:
/// "Fast Compression Method for Medical Images on the Web" by Bas Hulsken
/// https://arxiv.org/abs/2005.08713
class HulskenDecompressor {
 public:
  HulskenDecompressor() = default;

  /// @brief Decompress a Hulsken-compressed iSyntax codeblock
  ///
  /// The algorithm has 6 phases:
  /// 1. Parse version-specific header (bitmasks, zero run parameters)
  /// 2. Build Huffman decoding table from embedded Huffman tree
  /// 3. Decode Huffman message with run-length encoding (RLE) for zeros
  /// 4. Post-process version 1 bitmasks (if needed)
  /// 5. Unpack bitplanes into coefficient buffers
  /// 6. Reshuffle from 4x2 snake order and convert signed magnitude to two's
  /// complement
  ///
  /// @param compressed Compressed codeblock data
  /// @param block_width Width of the coefficient block
  /// @param block_height Height of the coefficient block
  /// @param coefficient Coefficient type: 0=LL (low-low), 1=H (high: LH+HL+HH)
  /// @param compressor_version Version 1 or 2 (different bitstream layouts)
  /// @param out_buffer Output buffer for decompressed coefficients
  /// @return Status indicating success or failure
  aifocore::Status Decompress(std::span<uint8_t> compressed,
                              int32_t block_width, int32_t block_height,
                              int32_t coefficient, int32_t compressor_version,
                              std::span<int16_t> out_buffer);

 private:
  aifocore::Status ParseCodeblockHeader(int32_t* bits_read);
  aifocore::Status BuildHuffmanTable(HuffmanTable* huffman, int32_t* bits_read);
  aifocore::Status DecodeHuffmanMessage(const HuffmanTable& huffman,
                                        int32_t* bits_read,
                                        ScopedArenaMemory* temp_memory,
                                        uint8_t** decompressed_buffer,
                                        int32_t* decompressed_length);
  aifocore::Status PostprocessVersion1Bitmasks(uint8_t* decompressed_buffer,
                                               int32_t decompressed_length);
  aifocore::Status UnpackBitplanes(uint8_t* decompressed_buffer,
                                   uint16_t* coeff_buffer);
  void ReshuffleAndConvert(uint16_t* coeff_buffer, int16_t* out_buffer);

  // Context state
  uint8_t* compressed_ = nullptr;
  size_t compressed_size_ = 0;
  int32_t block_width_ = 0;
  int32_t block_height_ = 0;
  int32_t coefficient_ = 0;
  int32_t compressor_version_ = 0;

  int32_t coeff_count_ = 0;
  int32_t coeff_bit_depth_ = 16;
  size_t coeff_buffer_size_ = 0;
  int32_t block_size_in_bits_ = 0;
  int64_t serialized_length_ = 0;
  std::array<uint32_t, 3> bitmasks_ = {0xFFFF, 0xFFFF, 0xFFFF};
  int32_t total_mask_bits_ = 0;
  uint8_t zerorun_symbol_ = 0;
  uint8_t zero_counter_size_ = 0;
  std::array<uint32_t, 16> bitplane_offsets_ = {};
};

// Convenience wrapper (alloc-free) around `HulskenDecompressor`.
aifocore::Status HulskenDecompress(std::span<uint8_t> compressed,
                                   int32_t block_width, int32_t block_height,
                                   int32_t coefficient,
                                   int32_t compressor_version,
                                   std::span<int16_t> out_buffer);

}  // namespace isyntax
