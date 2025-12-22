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

#pragma once

#include "aifocore/status/result.h"
#include "readers/isyntax/third_party/isyntax.h"

namespace isyntax {
namespace open {

void InitializeLevelGeometry(isyntax_t* isyntax, isyntax_image_t* wsi_image);
void ProcessCodeBlocks(isyntax_t* isyntax, isyntax_image_t* wsi_image);
aifocore::Status CreateTileLookupTables(isyntax_t* isyntax,
                                        isyntax_image_t* wsi_image);
aifocore::Status InitializeBlockAllocators(isyntax_t* isyntax,
                                           isyntax_open_flags_t flags);
aifocore::Status InitializeDummyBlocks(isyntax_t* isyntax);

}  // namespace open
}  // namespace isyntax
