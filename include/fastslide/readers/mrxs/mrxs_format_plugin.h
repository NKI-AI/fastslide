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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_FORMAT_PLUGIN_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_FORMAT_PLUGIN_H_

#include <cstddef>
#include <memory>
#include <string_view>

#include "absl/status/statusor.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

/**
 * @file mrxs_format_plugin.h
 * @brief MRXS format plugin descriptor
 * 
 * This header provides the format descriptor for MRXS (3DHISTECH) slides.
 * It encapsulates all MRXS-specific logic in a self-contained plugin that
 * can be registered with the reader registry.
 */

namespace fastslide::formats::mrxs {

/// @brief Create MRXS format descriptor
///
/// Creates a FormatDescriptor for the MRXS format with appropriate
/// capabilities, extensions, and factory function.
///
/// @return FormatDescriptor for MRXS format
FormatDescriptor CreateMrxsFormatDescriptor();

/// @brief Helper function to register MRXS format
///
/// Convenience function that creates and returns a descriptor ready for
/// registration with the reader registry.
///
/// Example usage:
/// @code
/// auto descriptor = RegisterMrxsFormat();
/// registry.RegisterFormat(descriptor);
/// @endcode
inline FormatDescriptor RegisterMrxsFormat() {
  return CreateMrxsFormatDescriptor();
}

}  // namespace fastslide::formats::mrxs

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_FORMAT_PLUGIN_H_
