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

#pragma once

#include <cstdint>

namespace isyntax {
namespace chunk {

// Number of codeblocks per color component in a chunk at a given level.
// This mirrors the legacy behavior from `isyntax.c`.
[[nodiscard]] constexpr int32_t GetChunkCodeblocksPerColorForLevel(
    int32_t level, bool has_ll) {
  const int32_t rel_level = level % 3;
  int32_t codeblock_count = 0;
  if (rel_level == 0) {
    codeblock_count = 1;
  } else if (rel_level == 1) {
    codeblock_count = 1 + 4;
  } else {
    codeblock_count = 1 + 4 + 16;
  }
  if (has_ll) {
    ++codeblock_count;
  }
  return codeblock_count;
}

}  // namespace chunk
}  // namespace isyntax
