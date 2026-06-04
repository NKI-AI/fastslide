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

#include <cstdio>

#include "aifocore/status/result.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"

namespace isyntax::xml {

// Semantic handler for top-level UFSImport elements (node_stack_index == 2).
aifocore::Status ParseUfsimportChildNode(isyntax_t* isyntax, uint32_t group,
                                         uint32_t element, char* value,
                                         uint64_t value_len);

// Semantic handler for scanned-image elements (node_stack_index != 2).
aifocore::Status ParseScannedimageChildNode(isyntax_t* isyntax, uint32_t group,
                                            uint32_t element, char* value,
                                            uint64_t value_len);

}  // namespace isyntax::xml
