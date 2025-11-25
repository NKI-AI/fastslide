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

#include "simpletiff/lzw.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace simpletiff {

namespace {

// -----------------------------------------------------------
// LZW table entry
// -----------------------------------------------------------
struct LzwTableEntry {
  std::vector<uint8_t> data;
};

// MSB bit reader macro (TIFF LZW)
#define LZW_GET_NEXT_CODE_MSB(code, src, srcbitsize, bitcount, bitw, shr, \
                              mask)                                       \
  do {                                                                    \
    if ((bitcount + bitw) <= srcbitsize) {                                \
      const uint32_t bitoffset = bitcount & 0x7;                          \
      const uint8_t* bytes = src + (bitcount >> 3);                       \
      if (bitoffset == 0 && bitw <= 24) {                                 \
        code = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];             \
        code >>= (24 - bitw);                                             \
      } else {                                                            \
        code = static_cast<uint32_t>(bytes[0]);                           \
        code <<= 8;                                                       \
        code |= static_cast<uint32_t>(bytes[1]);                          \
        code <<= 8;                                                       \
        if ((bitcount + 24) <= srcbitsize) {                              \
          code |= static_cast<uint32_t>(bytes[2]);                        \
        }                                                                 \
        code <<= 8;                                                       \
        code <<= bitoffset;                                               \
        code &= mask;                                                     \
        code >>= shr;                                                     \
      }                                                                   \
      bitcount += bitw;                                                   \
    } else {                                                              \
      code = 257; /* EOI */                                               \
    }                                                                     \
  } while (0)

}  // namespace

// -----------------------------------------------------------
// TIFF LZW decompressor (matching imagecodecs implementation)
// -----------------------------------------------------------
bool DecompressLzw(std::span<const uint8_t> compressed,
                   std::vector<uint8_t>& decompressed) {
  constexpr uint32_t kClear = 256;
  constexpr uint32_t kEoi = 257;
  constexpr uint32_t kTableSize = 5120;  // 4098 + 1024 buffer for old style

  if (compressed.empty()) {
    decompressed.clear();
    return true;
  }

  const uint8_t* src = compressed.data();
  const size_t srcsize = compressed.size();

  if (srcsize < 2) {
    return false;
  }

  // Check for MSB encoding (TIFF) - must start with CLEAR code (0x80 0x0?)
  if ((src[0] != 128) || (src[1] & 128)) {
    return false;
  }

  try {
    // Allocate LZW table - each entry stores the decoded sequence
    std::vector<LzwTableEntry> table(kTableSize);

    // Initialize first 256 entries (single bytes)
    for (uint32_t i = 0; i < 256; ++i) {
      table[i].data.resize(1);
      table[i].data[0] = static_cast<uint8_t>(i);
    }

    decompressed.clear();
    decompressed.reserve(srcsize * 3);

    uint32_t tablesize = 258;
    uint32_t code = 0;
    uint32_t oldcode = 0;
    uint32_t shr = 23;
    uint32_t mask = 4286578688u;  // 0xFF800000
    uint64_t bitw = 9;
    uint64_t bitcount = 0;
    const uint64_t srcbitsize = srcsize * 8;

    while (true) {
      // Read next code (MSB - TIFF style)
      LZW_GET_NEXT_CODE_MSB(code, src, srcbitsize, bitcount, bitw, shr, mask);

      if (code == kEoi)
        break;

      if (code == kClear) {
        // Reset table to initial state
        tablesize = 258;
        bitw = 9;
        shr = 23;
        mask = 4286578688u;

        // Read next code after CLEAR (skip multiple CLEARs)
        do {
          LZW_GET_NEXT_CODE_MSB(code, src, srcbitsize, bitcount, bitw, shr,
                                mask);
        } while (code == kClear);

        if (code == kEoi)
          break;

        if (code >= 256) {
          return false;  // Invalid code immediately after CLEAR
        }

        decompressed.push_back(static_cast<uint8_t>(code));
        oldcode = code;
        continue;
      }

      if (tablesize >= kTableSize) {
        return false;  // Table overflow
      }

      if (code < tablesize) {
        // Code is in table - output its sequence
        const auto& entry = table[code].data;
        decompressed.insert(decompressed.end(), entry.begin(), entry.end());

        // Create new table entry: oldcode's sequence + first byte of code's sequence
        table[tablesize].data = table[oldcode].data;
        table[tablesize].data.push_back(entry[0]);

      } else if (code == tablesize) {
        // Code not in table yet (special KwKwK case)
        // Output: oldcode's sequence + first byte of oldcode's sequence
        const auto& old_entry = table[oldcode].data;
        decompressed.insert(decompressed.end(), old_entry.begin(),
                            old_entry.end());
        decompressed.push_back(old_entry[0]);

        // Create new table entry: same as what we just output
        table[tablesize].data = old_entry;
        table[tablesize].data.push_back(old_entry[0]);

      } else {
        // code > tablesize - corrupt stream
        return false;
      }

      tablesize++;
      oldcode = code;

      // Increase bit-width if necessary (early change for MSB/TIFF)
      // This matches imagecodecs imcd.c lines 2322-2333
      switch (tablesize) {
        case 511:
          bitw = 10;
          shr = 22;
          mask = 4290772992u;  // 0xFFC00000
          break;
        case 1023:
          bitw = 11;
          shr = 21;
          mask = 4292870144u;  // 0xFFE00000
          break;
        case 2047:
          bitw = 12;
          shr = 20;
          mask = 4293918720u;  // 0xFFF00000
          break;
      }
    }

    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace simpletiff
