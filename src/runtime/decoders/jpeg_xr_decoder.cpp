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

#include "fastslide/runtime/decoders/jpeg_xr_decoder.h"

#include <cstdint>
#include <vector>

// `jxrlib`'s `guiddef.h` uses `__ANSI__` to decide whether `FAR` is empty or
// expands to `_far`. On non-Windows platforms, `_far` is invalid and breaks
// parsing of `DEFINE_GUID(...)` declarations in `JXRGlue.h`.
//
// Bazel builds already pass `-D__ANSI__` for this compilation unit; define it
// here as well so Meson (and other build systems) behave consistently.
#ifndef __ANSI__
#define __ANSI__ 1
#endif

extern "C" {
#include <JXRGlue.h>
}

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "aifocore/status/result.h"

namespace fastslide::runtime::decoders {

aifocore::Result<DecodedRgb> DecodeJpegXrToRgb(
    std::span<const uint8_t> jxr_bytes,
    std::optional<ExpectedDimensions> expected) {
  if (jxr_bytes.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "JPEG-XR input is empty");
  }

  PKFactory* factory = nullptr;
  PKCodecFactory* codec_factory = nullptr;
  WMPStream* stream = nullptr;
  PKImageDecode* decoder = nullptr;
  PKFormatConverter* converter = nullptr;

  auto cleanup = [&]() {
    if (converter != nullptr) {
      converter->Release(&converter);
    }
    if (decoder != nullptr) {
      decoder->Release(&decoder);
    }
    if (stream != nullptr) {
      stream->Close(&stream);
    }
    if (codec_factory != nullptr) {
      codec_factory->Release(&codec_factory);
    }
    if (factory != nullptr) {
      factory->Release(&factory);
    }
  };

  if (PKCreateFactory(&factory, PK_SDK_VERSION) != WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "PKCreateFactory failed");
  }
  if (PKCreateCodecFactory(&codec_factory, PK_SDK_VERSION) != WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "PKCreateCodecFactory failed");
  }

  // Create memory stream over input buffer.
  if (factory->CreateStreamFromMemory(&stream,
                                      const_cast<uint8_t*>(jxr_bytes.data()),
                                      jxr_bytes.size()) != WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "CreateStreamFromMemory failed");
  }

  if (codec_factory->CreateCodec(&IID_PKImageWmpDecode,
                                 reinterpret_cast<void**>(&decoder)) !=
      WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "CreateCodec(WmpDecode) failed");
  }

  if (decoder->Initialize(decoder, stream) != WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "JPEG-XR decoder initialize failed");
  }
  decoder->fStreamOwner = 0;  // stream owned by us

  if (codec_factory->CreateFormatConverter(&converter) != WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "CreateFormatConverter failed");
  }

  // Convert to RGB8 always.
  char ext[] = ".jxr";
  if (converter->Initialize(converter, decoder, ext,
                            GUID_PKPixelFormat24bppRGB) != WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "FormatConverter initialize failed");
  }

  I32 w = 0;
  I32 h = 0;
  if (converter->GetSize(converter, &w, &h) != WMP_errSuccess || w <= 0 ||
      h <= 0) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "JPEG-XR GetSize failed");
  }

  if (expected.has_value()) {
    if (static_cast<uint32_t>(w) != expected->width ||
        static_cast<uint32_t>(h) != expected->height) {
      cleanup();
      return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                              "JPEG-XR decoded size mismatch");
    }
  }

  DecodedRgb out{};
  out.width = static_cast<uint32_t>(w);
  out.height = static_cast<uint32_t>(h);
  out.rgb.resize(static_cast<size_t>(w) * h * 3);

  PKRect rect{0, 0, w, h};
  if (converter->Copy(converter, &rect, out.rgb.data(),
                      static_cast<U32>(w) * 3) != WMP_errSuccess) {
    cleanup();
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "JPEG-XR Copy failed");
  }

  cleanup();
  return out;
}

}  // namespace fastslide::runtime::decoders
