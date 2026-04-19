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
#include <string_view>

#include "aifocore/status/result.h"

struct isyntax_t;
struct isyntax_cache_t;

namespace isyntax {

/// RAII wrapper around the internal coefficient-tile cache used during decode.
class IsyntaxCache {
 public:
  static aifocore::Result<std::unique_ptr<IsyntaxCache>> CreateAndInject(
      std::string_view debug_name_or_null, int32_t cache_size,
      isyntax_t* isyntax);

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
