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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_FORMAT_PLUGIN_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_FORMAT_PLUGIN_H_

#include "fastslide/runtime/format_descriptor.h"

namespace fastslide::formats::omezarr {

/// @brief Build the OME-Zarr format descriptor used at registration time.
FormatDescriptor CreateOmezarrFormatDescriptor();

/// @brief Convenience alias used by static-init registrars.
inline FormatDescriptor RegisterOmezarrFormat() {
  return CreateOmezarrFormatDescriptor();
}

}  // namespace fastslide::formats::omezarr

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_FORMAT_PLUGIN_H_
