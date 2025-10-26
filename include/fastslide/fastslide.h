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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_FASTSLIDE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_FASTSLIDE_H_

/**
 * @file fastslide.h
 * @brief Main header for the FastSlide library
 * 
 * This header includes all the core FastSlide functionality for reading
 * digital pathology slides in various formats (MRXS, QPTIFF, SVS).
 * 
 * ## Architecture (v2.0)
 * 
 * FastSlide has a clean 3-layer architecture:
 * - **Core**: Pure domain models (metadata, requests, descriptors, plans)
 * - **Runtime**: Services and infrastructure (caching, registry, I/O, decoders)
 * - **Formats**: Format-specific plugins (MRXS, QPTIFF, SVS)
 * 
 * @see fastslide/core/ for domain models
 * @see fastslide/runtime/ for runtime services
 * @see fastslide/readers/ for format readers and plugins
 */

// ============================================================================
// Core Domain Models
// ============================================================================

#include "fastslide/core/metadata.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"

// ============================================================================
// Runtime Services
// ============================================================================

#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/runtime/lru_tile_cache.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/runtime/tile_writer.h"

// ============================================================================
// Format Plugins
// ============================================================================

#include "fastslide/readers/readers.h"

// ============================================================================
// Public API
// ============================================================================

#include "fastslide/image.h"
#include "fastslide/metadata.h"
#include "fastslide/runtime/plugin_loader.h"
#include "fastslide/slide_options.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/cache.h"
#include "fastslide/utilities/colors.h"

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_FASTSLIDE_H_
