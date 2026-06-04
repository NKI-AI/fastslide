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
#include <optional>
#include <span>
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
#include "aifocore/utilities/fmt.h"

namespace fastslide::runtime::decoders {

namespace {

/// @brief Owning RAII wrapper around the jxrlib decode pipeline.
///
/// Wraps the four objects (factory, codec factory, stream, decoder) plus the
/// format converter, so callers can both probe the decoded dimensions and
/// copy pixels into a caller-allocated buffer without juggling manual
/// `Release` calls in every error path.
class JxrDecodeSession {
 public:
  JxrDecodeSession() = default;
  JxrDecodeSession(const JxrDecodeSession&) = delete;
  JxrDecodeSession& operator=(const JxrDecodeSession&) = delete;
  JxrDecodeSession(JxrDecodeSession&&) = delete;
  JxrDecodeSession& operator=(JxrDecodeSession&&) = delete;

  ~JxrDecodeSession() {
    if (converter_ != nullptr) {
      converter_->Release(&converter_);
    }
    if (decoder_ != nullptr) {
      decoder_->Release(&decoder_);
    }
    if (stream_ != nullptr) {
      stream_->Close(&stream_);
    }
    if (codec_factory_ != nullptr) {
      codec_factory_->Release(&codec_factory_);
    }
    if (factory_ != nullptr) {
      factory_->Release(&factory_);
    }
  }

  /// @brief Open @p jxr_bytes and probe the encoded pixel format.
  ///
  /// After this call returns OK, `source_format()`, `width()` and `height()`
  /// reflect the bitstream as encoded. The caller must invoke
  /// `ConfigureConverter` with the desired target format before calling
  /// `Copy`.
  aifocore::Status Open(std::span<const uint8_t> jxr_bytes) {
    if (jxr_bytes.empty()) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "JPEG-XR input is empty");
    }
    if (PKCreateFactory(&factory_, PK_SDK_VERSION) != WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "PKCreateFactory failed");
    }
    if (PKCreateCodecFactory(&codec_factory_, PK_SDK_VERSION) !=
        WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "PKCreateCodecFactory failed");
    }
    if (factory_->CreateStreamFromMemory(&stream_,
                                         const_cast<uint8_t*>(jxr_bytes.data()),
                                         jxr_bytes.size()) != WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "CreateStreamFromMemory failed");
    }
    if (codec_factory_->CreateCodec(&IID_PKImageWmpDecode,
                                    reinterpret_cast<void**>(&decoder_)) !=
        WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "CreateCodec(WmpDecode) failed");
    }
    if (decoder_->Initialize(decoder_, stream_) != WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "JPEG-XR decoder initialize failed");
    }
    decoder_->fStreamOwner = 0;

    if (decoder_->GetPixelFormat(decoder_, &source_format_) != WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "JPEG-XR GetPixelFormat failed");
    }
    I32 w = 0;
    I32 h = 0;
    if (decoder_->GetSize(decoder_, &w, &h) != WMP_errSuccess || w <= 0 ||
        h <= 0) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "JPEG-XR decoder GetSize failed");
    }
    width_ = static_cast<uint32_t>(w);
    height_ = static_cast<uint32_t>(h);
    return aifocore::Status::OkStatus();
  }

  /// @brief Build a `PKFormatConverter` from the source format to
  ///        @p target_format.
  ///
  /// `target_format` must be reachable from `source_format()` via jxrlib's
  /// conversion routing table; passing `source_format()` itself is always
  /// valid (identity copy).
  aifocore::Status ConfigureConverter(const PKPixelFormatGUID& target_format) {
    if (codec_factory_->CreateFormatConverter(&converter_) != WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "CreateFormatConverter failed");
    }
    char ext[] = ".jxr";
    if (converter_->Initialize(converter_, decoder_, ext, target_format) !=
        WMP_errSuccess) {
      // jxrlib pixel format GUIDs share their first 11 bytes; only `Data4[7]`
      // distinguishes them. Surface both source and target tags so callers can
      // diagnose unsupported conversions.
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format(
              "FormatConverter initialize failed (source=0x{:02x}, "
              "target=0x{:02x})",
              static_cast<unsigned>(source_format_.Data4[7]),
              static_cast<unsigned>(target_format.Data4[7])));
    }
    return aifocore::Status::OkStatus();
  }

  [[nodiscard]] uint32_t width() const { return width_; }

  [[nodiscard]] uint32_t height() const { return height_; }

  [[nodiscard]] const PKPixelFormatGUID& source_format() const {
    return source_format_;
  }

  /// @brief Copy the full image into @p dst (must hold
  ///        `width * height * bytes_per_pixel` bytes).
  aifocore::Status Copy(void* dst, uint32_t bytes_per_pixel) {
    PKRect rect{0, 0, static_cast<I32>(width_), static_cast<I32>(height_)};
    if (converter_->Copy(converter_, &rect, static_cast<U8*>(dst),
                         width_ * bytes_per_pixel) != WMP_errSuccess) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "JPEG-XR Copy failed");
    }
    return aifocore::Status::OkStatus();
  }

 private:
  PKFactory* factory_ = nullptr;
  PKCodecFactory* codec_factory_ = nullptr;
  WMPStream* stream_ = nullptr;
  PKImageDecode* decoder_ = nullptr;
  PKFormatConverter* converter_ = nullptr;
  PKPixelFormatGUID source_format_ = GUID_PKPixelFormatDontCare;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
};

aifocore::Status CheckExpected(const JxrDecodeSession& session,
                               const std::optional<ExpectedDimensions>& exp) {
  if (!exp.has_value()) {
    return aifocore::Status::OkStatus();
  }
  if (session.width() != exp->width || session.height() != exp->height) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "JPEG-XR decoded size mismatch");
  }
  return aifocore::Status::OkStatus();
}

// `IsEqualGUID` from jxrlib's guiddef.h takes its arguments by value (REFGUID
// expands to `const GUID` on non-Windows builds), so callers must pass the
// GUID itself rather than a pointer.
bool IsGray16(const PKPixelFormatGUID& guid) {
  return IsEqualGUID(guid, GUID_PKPixelFormat16bppGray) != 0;
}

bool IsGray8(const PKPixelFormatGUID& guid) {
  return IsEqualGUID(guid, GUID_PKPixelFormat8bppGray) != 0;
}

/// @brief True if @p guid stores three interleaved 16-bit samples per pixel.
///
/// Both `GUID_PKPixelFormat48bppRGB` and `GUID_PKPixelFormat48bpp3Channels`
/// share the same in-memory layout (3 x uint16 little-endian, interleaved),
/// so for our purposes they can be consumed identically. 3DHISTECH stores
/// fluorescence MRXS tiles as `48bpp3Channels`, which jxrlib's converter
/// cannot remap to `48bppRGB` (no entry in its routing table), but an
/// identity copy of the raw samples is fine.
bool IsThreeChannel48(const PKPixelFormatGUID& guid) {
  return IsEqualGUID(guid, GUID_PKPixelFormat48bppRGB) != 0 ||
         IsEqualGUID(guid, GUID_PKPixelFormat48bpp3Channels) != 0;
}

/// @brief True if @p guid stores three interleaved 8-bit samples per pixel.
bool IsThreeChannel24(const PKPixelFormatGUID& guid) {
  return IsEqualGUID(guid, GUID_PKPixelFormat24bppRGB) != 0 ||
         IsEqualGUID(guid, GUID_PKPixelFormat24bpp3Channels) != 0;
}

}  // namespace

aifocore::Result<DecodedRgb> DecodeJpegXrToRgb(
    std::span<const uint8_t> jxr_bytes,
    std::optional<ExpectedDimensions> expected) {
  JxrDecodeSession session;
  AIFOCORE_RETURN_IF_ERROR(session.Open(jxr_bytes));
  AIFOCORE_RETURN_IF_ERROR(CheckExpected(session, expected));

  DecodedRgb out{};
  out.width = session.width();
  out.height = session.height();
  const size_t pixels = static_cast<size_t>(out.width) * out.height;
  out.rgb.assign(pixels * 3U, 0);

  if (IsGray8(session.source_format())) {
    // Single-channel 8-bit grayscale: decode straight into a scratch buffer
    // then replicate into all three RGB planes.
    AIFOCORE_RETURN_IF_ERROR(
        session.ConfigureConverter(session.source_format()));
    std::vector<uint8_t> gray(pixels, 0);
    AIFOCORE_RETURN_IF_ERROR(session.Copy(gray.data(), 1U));
    for (size_t i = 0; i < pixels; ++i) {
      const uint8_t value = gray[i];
      out.rgb[(i * 3U) + 0] = value;
      out.rgb[(i * 3U) + 1] = value;
      out.rgb[(i * 3U) + 2] = value;
    }
    return out;
  }

  if (IsThreeChannel24(session.source_format())) {
    // Layout-compatible with 24bppRGB: identity copy.
    AIFOCORE_RETURN_IF_ERROR(
        session.ConfigureConverter(session.source_format()));
    AIFOCORE_RETURN_IF_ERROR(session.Copy(out.rgb.data(), 3U));
    return out;
  }

  // Last resort: ask jxrlib to convert to 24bpp RGB through its routing table.
  AIFOCORE_RETURN_IF_ERROR(
      session.ConfigureConverter(GUID_PKPixelFormat24bppRGB));
  AIFOCORE_RETURN_IF_ERROR(session.Copy(out.rgb.data(), 3U));
  return out;
}

aifocore::Result<DecodedRgb16> DecodeJpegXrToRgb16(
    std::span<const uint8_t> jxr_bytes,
    std::optional<ExpectedDimensions> expected) {
  JxrDecodeSession session;
  AIFOCORE_RETURN_IF_ERROR(session.Open(jxr_bytes));
  AIFOCORE_RETURN_IF_ERROR(CheckExpected(session, expected));

  DecodedRgb16 out{};
  out.width = session.width();
  out.height = session.height();
  const size_t pixels = static_cast<size_t>(out.width) * out.height;
  out.rgb.assign(pixels * 3U, 0);

  if (IsGray16(session.source_format())) {
    // Single-channel 16-bit grayscale: some MRXS fluorescence tiles are
    // stored this way. Decode to a `uint16_t` scratch buffer and replicate
    // into all three planes so downstream consumers (which interpret the
    // buffer as interleaved RGB16) see consistent values regardless of
    // which plane they de-interleave from.
    AIFOCORE_RETURN_IF_ERROR(
        session.ConfigureConverter(session.source_format()));
    std::vector<uint16_t> gray(pixels, 0);
    AIFOCORE_RETURN_IF_ERROR(session.Copy(gray.data(), 2U));
    for (size_t i = 0; i < pixels; ++i) {
      const uint16_t value = gray[i];
      out.rgb[(i * 3U) + 0] = value;
      out.rgb[(i * 3U) + 1] = value;
      out.rgb[(i * 3U) + 2] = value;
    }
    return out;
  }

  if (IsThreeChannel48(session.source_format())) {
    // 48bppRGB and 48bpp3Channels share the same memory layout (3 x uint16
    // interleaved, native endian). 3DHISTECH fluorescence tiles use
    // `48bpp3Channels`, which jxrlib's converter cannot remap to `48bppRGB`,
    // so we configure the converter for an identity copy and write the
    // samples straight into the RGB16 output buffer.
    AIFOCORE_RETURN_IF_ERROR(
        session.ConfigureConverter(session.source_format()));
    AIFOCORE_RETURN_IF_ERROR(session.Copy(out.rgb.data(), 6U));
    return out;
  }

  // Last resort: ask jxrlib to convert to 48bpp RGB through its routing
  // table. Will return a descriptive error if the source format is not
  // reachable from `48bppRGB`.
  AIFOCORE_RETURN_IF_ERROR(
      session.ConfigureConverter(GUID_PKPixelFormat48bppRGB));
  AIFOCORE_RETURN_IF_ERROR(session.Copy(out.rgb.data(), 6U));
  // jxrlib decodes 48bppRGB into native-endian `uint16_t` samples so no
  // byte swap is needed here.
  return out;
}

}  // namespace fastslide::runtime::decoders
