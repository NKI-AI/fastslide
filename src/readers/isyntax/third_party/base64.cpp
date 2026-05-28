//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice,
//  this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/isyntax/third_party/base64.h"

namespace isyntax {
namespace base64 {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr uint8_t kInvalid = 0xFF;

constexpr std::array<uint8_t, 256> BuildDecodeTable() {
  std::array<uint8_t, 256> table{};
  table.fill(kInvalid);
  for (size_t i = 0; i < kAlphabet.size(); ++i) {
    table[static_cast<uint8_t>(kAlphabet[i])] = static_cast<uint8_t>(i);
  }
  return table;
}

constexpr std::array<uint8_t, 256> kDecodeTable = BuildDecodeTable();

}  // namespace

/* Decoder ported and modified from Jouni Malinen's base64.c.
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
 * Under original license: BSD 2-Clause License
 */

aifocore::Result<std::vector<uint8_t>> DecodeBytes(
    std::span<const uint8_t> src) {
  // Single-pass decode. We skip bytes not in the base64 alphabet, matching the
  // legacy behavior.
  //
  // iSyntax quirk: some streams have a *spurious trailing '/'*. Since '/' is a
  // valid base64 character, we can't skip it unconditionally without breaking
  // legitimate base64. Instead, we treat a *dangling final '/'* (the only
  // leftover sextet at end-of-stream) as ignorable.
  if (src.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid base64 input");
  }

  // Allocate a conservative upper bound (may over-allocate if input contains
  // whitespace/other ignored characters). This avoids `push_back` overhead.
  const size_t out_capacity = ((src.size() + 3) / 4) * 3;
  std::vector<uint8_t> out(out_capacity);
  size_t out_pos = 0;

  uint8_t quartet[4] = {0, 0, 0, 0};
  int quartet_count = 0;
  int pad = 0;
  bool saw_any = false;
  bool pad_in_quartet = false;

  const uint8_t* p = src.data();
  const uint8_t* end = p + src.size();
  for (; p != end; ++p) {
    const uint8_t ch = *p;
    if (ch == static_cast<uint8_t>('=')) {
      saw_any = true;
      pad_in_quartet = true;
      ++pad;
      quartet[quartet_count++] = 0;
    } else {
      const uint8_t v = kDecodeTable[ch];
      if (v == kInvalid) {
        continue;
      }
      saw_any = true;
      if (pad_in_quartet) {
        return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                                "Invalid base64 padding");
      }
      quartet[quartet_count++] = v;
    }

    if (quartet_count == 4) {
      const uint8_t b0 =
          static_cast<uint8_t>((quartet[0] << 2) | (quartet[1] >> 4));
      const uint8_t b1 =
          static_cast<uint8_t>((quartet[1] << 4) | (quartet[2] >> 2));
      const uint8_t b2 = static_cast<uint8_t>((quartet[2] << 6) | quartet[3]);

      if (pad == 0) {
        out[out_pos++] = b0;
        out[out_pos++] = b1;
        out[out_pos++] = b2;
      } else if (pad == 1) {
        out[out_pos++] = b0;
        out[out_pos++] = b1;
        break;
      } else if (pad == 2) {
        out[out_pos++] = b0;
        break;
      } else {
        return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                                "Invalid base64 padding");
      }

      quartet_count = 0;
      pad = 0;
      pad_in_quartet = false;
    }
  }

  if (!saw_any) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid base64 input");
  }

  // Allow a single dangling '/' at end-of-stream (common in iSyntax metadata).
  // If the stream is only "/", treat it as invalid (legacy behavior).
  if (quartet_count == 1 && pad == 0 && quartet[0] == 63) {
    if (out_pos == 0) {
      return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                              "Invalid base64 input");
    }
    quartet_count = 0;
  }

  if (quartet_count != 0 && pad == 0) {
    // Incomplete quartet without padding.
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid base64 input");
  }
  out.resize(out_pos);
  return out;
}

}  // namespace base64

aifocore::Result<std::vector<uint8_t>> Base64Decode(
    std::span<const uint8_t> src) {
  return base64::DecodeBytes(src);
}

}  // namespace isyntax
