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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_ACCUMULATE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_ACCUMULATE_H_

#include <cstdint>

#include "absl/synchronization/mutex.h"
#include "fastslide/runtime/tile_writer/fs_profile.h"

namespace fastslide::runtime {

/// @brief Accumulate weighted linear RGB tile into planar accumulators
/// (thread-safe)
///
/// Accumulates a source tile (in planar RGB format) into separate R, G, B
/// accumulator planes. Uses mutex protection for thread-safe parallel tile
/// writes. Planar storage enables sequential memory access patterns for better
/// cache locality and prefetching compared to interleaved storage.
///
/// @param linear_planar Source tile in linear RGB planar format (R plane, G
/// plane, B plane)
/// @param w Tile width
/// @param h Tile height
/// @param base_x Destination x coordinate in output image
/// @param base_y Destination y coordinate in output image
/// @param weight Tile weight for blending (typically from overlap region)
/// @param accumulator_r Output R channel accumulator (sized img_w * img_h +
/// kSimdPadding)
/// @param accumulator_g Output G channel accumulator (sized img_w * img_h +
/// kSimdPadding)
/// @param accumulator_b Output B channel accumulator (sized img_w * img_h +
/// kSimdPadding)
/// @param weight_sum Weight sum buffer (sized img_w * img_h + kSimdPadding)
/// @param img_w Output image width
/// @param img_h Output image height
/// @param accumulator_mutex Mutex to protect accumulator writes during parallel
/// execution
void AccumulateLinearTile(const float* linear_planar, int w, int h, int base_x,
                          int base_y, float weight, float* accumulator_r,
                          float* accumulator_g, float* accumulator_b,
                          float* weight_sum, int img_w, int img_h,
                          absl::Mutex& accumulator_mutex);

/// @brief Finalize: normalize planar linear RGB accumulators and convert to
/// interleaved sRGB8
///
/// Performs weighted normalization and colorspace conversion with
/// cache-blocking for optimal memory bandwidth. Processes image in 64x64 tiles
/// in parallel using a thread pool to maximize L1 cache hits and multi-core
/// utilization. Planar input enables sequential loads from each channel (3x
/// better cache utilization vs interleaved). Output is written in interleaved
/// RGB8 format for standard image APIs.
///
/// Memory access pattern (per 64x64 tile, processed in parallel):
/// 1. Sequential load R plane (cache-friendly)
/// 2. Sequential load G plane (cache-friendly)
/// 3. Sequential load B plane (cache-friendly)
/// 4. Sequential load weight plane
/// 5. Normalize: R/w, G/w, B/w (vectorized with fast reciprocal)
/// 6. Apply sRGB transfer function via LUT (vectorized gather)
/// 7. Interleave and store RGB8 (vectorized StoreInterleaved3)
///
/// Thread-safety: No mutex needed as each tile writes to non-overlapping output
/// regions.
///
/// @param accumulator_r Linear R channel accumulator (sized img_w * img_h +
/// kSimdPadding)
/// @param accumulator_g Linear G channel accumulator (sized img_w * img_h +
/// kSimdPadding)
/// @param accumulator_b Linear B channel accumulator (sized img_w * img_h +
/// kSimdPadding)
/// @param weight_sum Weight sum buffer (sized img_w * img_h + kSimdPadding)
/// @param img_w Output image width
/// @param img_h Output image height
/// @param out_interleaved Output buffer for interleaved RGB8 (sized img_w *
/// img_h * 3)
void FinalizeLinearToSrgb8(const float* accumulator_r,
                           const float* accumulator_g,
                           const float* accumulator_b, const float* weight_sum,
                           int img_w, int img_h, uint8_t* out_interleaved);

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_ACCUMULATE_H_
