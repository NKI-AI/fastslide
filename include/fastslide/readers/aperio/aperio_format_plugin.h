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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_FORMAT_PLUGIN_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_FORMAT_PLUGIN_H_

#include <cstddef>

#include "fastslide/runtime/format_descriptor.h"

/**
 * @file aperio_format_plugin.h
 * @brief Aperio format plugin descriptor
 * 
 * This header provides the format descriptor for Aperio slides.
 */

namespace fastslide::formats::aperio {

/// @brief Create Aperio format descriptor
FormatDescriptor CreateAperioFormatDescriptor();

/// @brief Helper function to register Aperio format
inline FormatDescriptor RegisterAperioFormat() {
  return CreateAperioFormatDescriptor();
}

}  // namespace fastslide::formats::aperio

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_FORMAT_PLUGIN_H_
