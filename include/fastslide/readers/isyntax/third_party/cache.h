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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "aifocore/status/result.h"

struct isyntax_t;
struct isyntax_cache_t;

namespace isyntax {

/// RAII wrapper around the internal coefficient-tile cache used during decode.
///
/// Files share one cache per block size (`Shared`), so that the coefficients
/// of every open file fit in one memory budget: the cache evicts the least
/// recently used tiles of any file.
class IsyntaxCache {
 public:
  /// Default capacity of the shared caches: 1 GiB.
  static constexpr size_t kDefaultSharedCapacityBytes = size_t{1} << 30;

  /// A cache for `isyntax` alone, keeping at most `capacity_bytes` of
  /// coefficients of tiles no read uses.
  static aifocore::Result<std::unique_ptr<IsyntaxCache>> CreateAndInject(
      std::string_view debug_name_or_null, size_t capacity_bytes,
      isyntax_t* isyntax);

  /// The cache shared by the files with the block size of `isyntax`, injected
  /// into it. Call `Release` before destroying `isyntax`.
  static aifocore::Result<std::shared_ptr<IsyntaxCache>> Shared(
      isyntax_t* isyntax);

  /// Sets the capacity of the shared caches, current and future. Caches over
  /// it evict tiles on their next read.
  static void SetSharedCapacityBytes(size_t capacity_bytes);
  static size_t SharedCapacityBytes();

  /// Frees the coefficients of the tiles of `isyntax` and forgets them. No read
  /// of `isyntax` may be running.
  void Release(isyntax_t* isyntax);

  IsyntaxCache(const IsyntaxCache&) = delete;
  IsyntaxCache& operator=(const IsyntaxCache&) = delete;
  IsyntaxCache(IsyntaxCache&&) = delete;
  IsyntaxCache& operator=(IsyntaxCache&&) = delete;

  ~IsyntaxCache();

  isyntax_cache_t* get() const { return handle_; }

 private:
  explicit IsyntaxCache(isyntax_cache_t* handle) : handle_(handle) {}

  isyntax_cache_t* handle_ = nullptr;
};

}  // namespace isyntax
