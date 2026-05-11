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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_PLAN_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_PLAN_CONTEXT_H_

#include <span>

#include "fastslide/readers/aperio/aperio_level_info.h"
#include "simpletiff/index.h"

namespace fastslide {

/// @brief Read-only view into the Aperio reader state needed by the plan
/// builder.
///
/// The plan builder used to take `const AperioReader&` and call
/// `reader.GetPyramidLevels()`, `reader.GetLevelCount()`,
/// `reader.GetLevelInfo()`, and `reader.GetTiffIndex()` on it. Those callbacks
/// created a call-graph cycle between `aperio.cpp` and
/// `aperio_plan_builder.cpp`. The reader now constructs an
/// `AperioPlanContext` once per `PrepareRequest` invocation and the planner
/// works exclusively against that view, so the dependency runs strictly
/// one-way.
struct AperioPlanContext {
  /// @brief Pyramid level descriptors. Span is valid for the duration of the
  /// planner call.
  std::span<const AperioLevelInfo> pyramid_levels;

  /// @brief Pre-parsed TIFF directory, owned by the reader.
  const simpletiff::TiffIndex& tiff_index;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_PLAN_CONTEXT_H_
