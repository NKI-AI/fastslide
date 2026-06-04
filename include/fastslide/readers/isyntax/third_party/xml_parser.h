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
//
// Original C code:
//   BSD 2-Clause License
//   Copyright (c) 2019-2025, Pieter Valkema

#pragma once

#include <cstdint>
#include <span>

#include "aifocore/status/result.h"

#include "fastslide/readers/isyntax/third_party/isyntax.h"

namespace isyntax {

/// Parses a chunk of the iSyntax XML header using the C++ implementation.
///
/// C++ XML header parsing entrypoint.
aifocore::Status ParseXmlHeaderChunk(isyntax_t* isyntax,
                                     std::span<const char> chunk,
                                     int64_t chunk_offset, bool is_last_chunk);

/// Destroys the internal C++ XML parser state stored on `isyntax_t`.
void DestroyXmlCppState(isyntax_t* isyntax);

}  // namespace isyntax
