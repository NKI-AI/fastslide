// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "fastslide/fastslide.h"

namespace fastslide::wasm {

namespace {

/// @brief Build a successful Result-shaped JS object: {ok: true, value: ...}.
emscripten::val Ok(emscripten::val value) {
  emscripten::val obj = emscripten::val::object();
  obj.set("ok", true);
  obj.set("value", std::move(value));
  return obj;
}

/// @brief Build a failure Result-shaped JS object: {ok: false, error: ...}.
emscripten::val Err(std::string_view message) {
  emscripten::val obj = emscripten::val::object();
  obj.set("ok", false);
  obj.set("error", std::string(message));
  return obj;
}

/// @brief Build a JS array of two numbers (used for [width, height] / mpp).
emscripten::val MakePair(double a, double b) {
  emscripten::val array = emscripten::val::array();
  array.call<void>("push", a);
  array.call<void>("push", b);
  return array;
}

/// @brief Run `fn` and turn any leaked C++ exception into an Err result.
///
/// The fastslide library still uses exceptions in a few spots (notably
/// `std::stoul`/`std::stod` inside metadata accessors). Catching at the
/// embind boundary keeps the wrapper's no-throw contract intact so JS
/// always sees a `{ok, value}` / `{ok, error}` object instead of a raw
/// exception pointer.
template <typename Fn>
emscripten::val CatchToErr(std::string_view context, Fn&& fn) {
  try {
    return fn();
  } catch (const std::exception& ex) {
    return Err(std::string(context) + ": " + ex.what());
  } catch (...) {
    return Err(std::string(context) + ": unknown C++ exception");
  }
}

}  // namespace

/// WASM wrapper for FastSlide level information (plain value object).
class FastSlideLevelInfo {
 public:
  FastSlideLevelInfo(uint32_t width, uint32_t height, double downsample_factor)
      : width_(width), height_(height), downsample_factor_(downsample_factor) {}

  uint32_t width() const { return width_; }

  uint32_t height() const { return height_; }

  double downsampleFactor() const { return downsample_factor_; }

 private:
  uint32_t width_;
  uint32_t height_;
  double downsample_factor_;
};

/// @brief WASM wrapper for FastSlide reader.
///
/// All methods follow a Result-style contract: they return a JS object of the
/// form `{ok: true, value: ...}` on success or `{ok: false, error: "..."}` on
/// failure, mirroring `aifocore::Result`. No method throws; embind exception
/// handling is intentionally disabled in BUILD.bazel.
class FastSlideReaderWrapper {
 public:
  /// @brief Factory: create reader from a virtual WORKERFS file path.
  /// @param file_path Path inside the emscripten virtual filesystem.
  /// @return `{ok: true, value: FastSlideReader}` or `{ok: false, error}`.
  static emscripten::val fromFilePath(const std::string& file_path) {
    return CatchToErr("fromFilePath", [&]() {
      auto reader_or =
          fastslide::runtime::GetGlobalRegistry().CreateReader(file_path);
      if (!reader_or.ok()) {
        return Err(std::string("Failed to open slide: ") +
                   std::string(reader_or.status().message()));
      }
      auto wrapper = std::make_shared<FastSlideReaderWrapper>(
          std::move(reader_or).value());
      return Ok(emscripten::val(wrapper));
    });
  }

  /// @brief Public constructor used by `std::make_shared` from `fromFilePath`.
  explicit FastSlideReaderWrapper(
      std::shared_ptr<fastslide::SlideReader> reader)
      : reader_(std::move(reader)) {}

  /// @brief Get number of pyramid levels.
  int numLevels() const { return reader_->GetLevelCount(); }

  /// @brief Get base dimensions (level 0) as `{ok, value: [w, h]}`.
  emscripten::val dimensions() const {
    return CatchToErr("dimensions", [&]() {
      auto level_info_or = reader_->GetLevelInfo(0);
      if (!level_info_or.ok()) {
        return Err(std::string("Failed to get dimensions: ") +
                   std::string(level_info_or.status().message()));
      }
      const auto& dims = level_info_or->dimensions;
      return Ok(MakePair(dims[0], dims[1]));
    });
  }

  /// @brief Get format name (e.g. "QPTIFF", "SVS").
  std::string format() const { return reader_->GetFormatName(); }

  /// @brief Get microns-per-pixel as `{ok, value: [mpp_x, mpp_y]}`.
  emscripten::val mpp() const {
    return CatchToErr("mpp", [&]() {
      const auto& props = reader_->GetProperties();
      return Ok(MakePair(props.mpp[0], props.mpp[1]));
    });
  }

  /// @brief Get level information as `{ok, value: FastSlideLevelInfo}`.
  emscripten::val getLevelInfo(int level) const {
    return CatchToErr("getLevelInfo", [&]() {
      auto level_info_or = reader_->GetLevelInfo(level);
      if (!level_info_or.ok()) {
        return Err(std::string("Failed to get level info: ") +
                   std::string(level_info_or.status().message()));
      }
      const auto& info = level_info_or.value();
      FastSlideLevelInfo wrapped(info.dimensions[0], info.dimensions[1],
                                 info.downsample_factor);
      return Ok(emscripten::val(wrapped));
    });
  }

  /// @brief Get level dimensions as `{ok, value: [w, h]}`.
  emscripten::val getLevelDimensions(int level) const {
    return CatchToErr("getLevelDimensions", [&]() {
      auto level_info_or = reader_->GetLevelInfo(level);
      if (!level_info_or.ok()) {
        return Err(std::string("Failed to get level dimensions: ") +
                   std::string(level_info_or.status().message()));
      }
      const auto& dims = level_info_or->dimensions;
      return Ok(MakePair(dims[0], dims[1]));
    });
  }

  /// @brief Read a region from the slide.
  /// @return `{ok: true, value: {width, height, channels, data}}` where `data`
  /// is a `Uint8Array` in interleaved HWC layout, or `{ok: false, error}`.
  ///
  /// Brightfield-only: rejects anything that isn't uint8 with 1, 3 or 4
  /// channels. Spectral / uint16 multiplex returns a descriptive error rather
  /// than silently rendering garbage. The image is forced to interleaved
  /// (HWC) layout via `MakeInterleaved()` before serialization so the JS
  /// viewer can blit it directly.
  emscripten::val readRegion(uint32_t x, uint32_t y, uint32_t width,
                             uint32_t height, int level) const {
    return CatchToErr("readRegion", [&]() {
      fastslide::RegionSpec region{
          .top_left = {x, y}, .size = {width, height}, .level = level};

      auto result = reader_->ReadRegion(region);
      if (!result.ok()) {
        return Err(std::string("ReadRegion failed: ") +
                   std::string(result.status().message()));
      }

      auto& image = result.value();

      if (image.GetDataType() != fastslide::DataType::kUInt8) {
        return Err(std::string("WASM viewer only supports uint8 brightfield "
                               "(got ") +
                   fastslide::GetDataTypeName(image.GetDataType()) + ")");
      }

      const uint32_t channels = image.GetChannels();
      if (channels != 1 && channels != 3 && channels != 4) {
        return Err(std::string("WASM viewer only supports 1/3/4-channel "
                               "brightfield (got ") +
                   std::to_string(channels) + ")");
      }

      image.MakeInterleaved();

      const uint8_t* raw_data = image.GetData();
      const size_t data_size = image.SizeBytes();

      auto view = emscripten::typed_memory_view(data_size, raw_data);
      emscripten::val data_array =
          emscripten::val::global("Uint8Array").new_(data_size);
      data_array.call<void>("set", view);

      emscripten::val payload = emscripten::val::object();
      payload.set("width", image.GetWidth());
      payload.set("height", image.GetHeight());
      payload.set("channels", channels);
      payload.set("data", data_array);
      return Ok(payload);
    });
  }

 private:
  std::shared_ptr<fastslide::SlideReader> reader_;
};

}  // namespace fastslide::wasm

// Emscripten bindings
EMSCRIPTEN_BINDINGS(fastslide) {
  emscripten::class_<fastslide::wasm::FastSlideLevelInfo>("FastSlideLevelInfo")
      .property("width", &fastslide::wasm::FastSlideLevelInfo::width)
      .property("height", &fastslide::wasm::FastSlideLevelInfo::height)
      .property("downsampleFactor",
                &fastslide::wasm::FastSlideLevelInfo::downsampleFactor);

  emscripten::class_<fastslide::wasm::FastSlideReaderWrapper>("FastSlideReader")
      .smart_ptr<std::shared_ptr<fastslide::wasm::FastSlideReaderWrapper>>(
          "FastSlideReaderShared")
      .class_function("fromFilePath",
                      &fastslide::wasm::FastSlideReaderWrapper::fromFilePath)
      .property("numLevels",
                &fastslide::wasm::FastSlideReaderWrapper::numLevels)
      .property("format", &fastslide::wasm::FastSlideReaderWrapper::format)
      .function("dimensions",
                &fastslide::wasm::FastSlideReaderWrapper::dimensions)
      .function("mpp", &fastslide::wasm::FastSlideReaderWrapper::mpp)
      .function("getLevelInfo",
                &fastslide::wasm::FastSlideReaderWrapper::getLevelInfo)
      .function("getLevelDimensions",
                &fastslide::wasm::FastSlideReaderWrapper::getLevelDimensions)
      .function("readRegion",
                &fastslide::wasm::FastSlideReaderWrapper::readRegion);
}
