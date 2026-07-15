// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

// Compares the lcms2 per-pixel ICC pass against the precomputed 256^3 LUT fast
// path in `IccTransform::ApplyInPlace`, for 8-bit RGB and RGBA regions at
// typical tile sizes. Also reports the one-time LUT build cost.
//
// Run with:
//   bazelisk run @fastslide/benchmarks:icc_transform_benchmark

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include <lcms2.h>

#include "benchmark/benchmark.h"
#include "fastslide/image.h"
#include "fastslide/slide_options.h"
#include "fastslide/utilities/color_transform.h"

namespace {

// Serialize a freshly built sRGB profile to stand in for an embedded slide
// profile. Using sRGB keeps the benchmark self-contained (no slide file).
std::vector<uint8_t> MakeSRGBProfileBytes() {
  cmsHPROFILE profile = cmsCreate_sRGBProfile();
  cmsUInt32Number size = 0;
  cmsSaveProfileToMem(profile, nullptr, &size);
  std::vector<uint8_t> bytes(size);
  cmsSaveProfileToMem(profile, bytes.data(), &size);
  cmsCloseProfile(profile);
  bytes.resize(size);
  return bytes;
}

// Build a spatially coherent 8-bit image that approximates the color locality
// of a real brightfield WSI tile: smooth low-frequency gradients plus a little
// per-pixel noise. Uniform random RGB is deliberately avoided because it is the
// pathological worst case for a 256^3 LUT (every gather is a cache miss), which
// does not reflect natural tile statistics.
fastslide::Image MakeTileLikeImage(uint32_t side,
                                   fastslide::ImageFormat format) {
  fastslide::Image image({side, side}, format, fastslide::DataType::kUInt8);
  const uint32_t channels = image.GetChannels();
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> noise(-6, 6);
  auto* data = image.GetData();
  for (uint32_t y = 0; y < side; ++y) {
    for (uint32_t x = 0; x < side; ++x) {
      const int base_r = 180 + static_cast<int>(40.0 * x / side);
      const int base_g = 150 + static_cast<int>(50.0 * y / side);
      const int base_b = 170 + static_cast<int>(30.0 * (x + y) / (2 * side));
      auto clamp = [](int v) {
        return static_cast<uint8_t>(std::max(0, std::min(255, v)));
      };
      uint8_t* px = data + (static_cast<size_t>(y) * side + x) * channels;
      px[0] = clamp(base_r + noise(rng));
      px[1] = clamp(base_g + noise(rng));
      px[2] = clamp(base_b + noise(rng));
      if (channels == 4) {
        px[3] = 255;  // Opaque alpha.
      }
    }
  }
  return image;
}

std::unique_ptr<fastslide::IccTransform> MakeTransform(bool build_lut) {
  const auto bytes = MakeSRGBProfileBytes();
  auto transform_or = fastslide::IccTransform::Create(
      bytes, fastslide::ColorSpace::kSRGB,
      fastslide::RenderingIntent::kPerceptual, build_lut);
  return std::move(transform_or.value());
}

void RunApply(benchmark::State& state, fastslide::ImageFormat format,
              bool use_lut) {
  const auto side = static_cast<uint32_t>(state.range(0));
  auto transform = MakeTransform(use_lut);
  const fastslide::Image original = MakeTileLikeImage(side, format);

  int64_t total_bytes = 0;
  for (auto _ : state) {
    // Re-seed the buffer each iteration so the transform is applied to fresh
    // (untransformed) pixels; the copy is excluded from the timed cost below.
    state.PauseTiming();
    fastslide::Image image = original;
    state.ResumeTiming();

    benchmark::DoNotOptimize(transform->ApplyInPlace(image));
    benchmark::DoNotOptimize(image.GetData());
    total_bytes += static_cast<int64_t>(image.SizeBytes());
  }
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(total_bytes);
}

void BM_Rgb_Lcms(benchmark::State& state) {
  RunApply(state, fastslide::ImageFormat::kRGB, /*use_lut=*/false);
}

void BM_Rgb_Lut(benchmark::State& state) {
  RunApply(state, fastslide::ImageFormat::kRGB, /*use_lut=*/true);
}

void BM_Rgba_Lcms(benchmark::State& state) {
  RunApply(state, fastslide::ImageFormat::kRGBA, /*use_lut=*/false);
}

void BM_Rgba_Lut(benchmark::State& state) {
  RunApply(state, fastslide::ImageFormat::kRGBA, /*use_lut=*/true);
}

// One-time cost of materializing the 256^3 LUT, reported separately so it can
// be weighed against the per-tile speedup.
void BM_LutBuild(benchmark::State& state) {
  const auto bytes = MakeSRGBProfileBytes();
  for (auto _ : state) {
    auto transform_or = fastslide::IccTransform::Create(
        bytes, fastslide::ColorSpace::kSRGB,
        fastslide::RenderingIntent::kPerceptual, /*build_lut=*/true);
    benchmark::DoNotOptimize(transform_or.value().get());
  }
}

BENCHMARK(BM_Rgb_Lcms)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Rgb_Lut)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Rgba_Lcms)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Rgba_Lut)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LutBuild)->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
