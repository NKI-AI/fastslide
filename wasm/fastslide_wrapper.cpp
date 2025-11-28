// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "fastslide/fastslide.h"
#include "fastslide/resample/average.h"

namespace fastslide::wasm {

/// WASM wrapper for FastSlide level information
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

/// WASM wrapper for FastSlide reader
class FastSlideReaderWrapper {
 public:
  /// Factory method: create reader from file path (for WORKERFS)
  static FastSlideReaderWrapper fromFilePath(const std::string& file_path) {
    // Create reader using runtime registry
    auto reader_or =
        fastslide::runtime::GetGlobalRegistry().CreateReader(file_path);

    if (!reader_or.ok()) {
      throw std::runtime_error("Failed to open slide: " +
                               std::string(reader_or.status().message()));
    }

    return FastSlideReaderWrapper(std::move(reader_or.value()));
  }

  /// Get number of pyramid levels
  int numLevels() const { return reader_->GetLevelCount(); }

  /// Get base dimensions (level 0) as array
  emscripten::val dimensions() const {
    auto level_info_or = reader_->GetLevelInfo(0);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get dimensions");
    }

    const auto& dims = level_info_or->dimensions;
    emscripten::val array = emscripten::val::array();
    array.call<void>("push", dims[0]);
    array.call<void>("push", dims[1]);
    return array;
  }

  /// Get format name
  std::string format() const { return reader_->GetFormatName(); }

  /// Get MPP (Microns Per Pixel)
  emscripten::val mpp() const {
    const auto& props = reader_->GetProperties();
    emscripten::val array = emscripten::val::array();
    array.call<void>("push", props.mpp[0]);
    array.call<void>("push", props.mpp[1]);
    return array;
  }

  /// Get level information
  FastSlideLevelInfo getLevelInfo(int level) const {
    auto level_info_or = reader_->GetLevelInfo(level);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level info");
    }

    const auto& info = level_info_or.value();
    return FastSlideLevelInfo(info.dimensions[0], info.dimensions[1],
                              info.downsample_factor);
  }

  /// Get level dimensions as array
  emscripten::val getLevelDimensions(int level) const {
    auto level_info_or = reader_->GetLevelInfo(level);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level dimensions");
    }

    const auto& dims = level_info_or->dimensions;
    emscripten::val array = emscripten::val::array();
    array.call<void>("push", dims[0]);
    array.call<void>("push", dims[1]);
    return array;
  }

  /// Read a region from the slide - returns JavaScript object with image data
  emscripten::val readRegion(uint32_t x, uint32_t y, uint32_t width,
                             uint32_t height, int level) const {
    fastslide::RegionSpec region{
        .top_left = {x, y}, .size = {width, height}, .level = level};

    auto result = reader_->ReadRegion(region);
    if (!result.ok()) {
      throw std::runtime_error("Failed to read region: " +
                               std::string(result.status().message()));
    }

    const auto& image = result.value();

    // Create JavaScript object with image properties
    emscripten::val obj = emscripten::val::object();
    obj.set("width", image.GetWidth());
    obj.set("height", image.GetHeight());
    obj.set("channels", image.GetChannels());

    // Copy image data to typed array
    const uint8_t* raw_data = image.GetData();
    size_t data_size = image.SizeBytes();

    emscripten::val data_array;
    switch (image.GetDataType()) {
      case fastslide::DataType::kUInt8: {
        auto view = emscripten::typed_memory_view(data_size, raw_data);
        data_array = emscripten::val::global("Uint8Array").new_(data_size);
        data_array.call<void>("set", view);
        break;
      }
      case fastslide::DataType::kUInt16: {
        size_t count = data_size / 2;
        const uint16_t* data_ptr = reinterpret_cast<const uint16_t*>(raw_data);
        auto view = emscripten::typed_memory_view(count, data_ptr);
        data_array = emscripten::val::global("Uint16Array").new_(count);
        data_array.call<void>("set", view);
        break;
      }
      default:
        throw std::runtime_error("Unsupported data type");
    }

    obj.set("data", data_array);
    return obj;
  }

 private:
  std::shared_ptr<fastslide::SlideReader> reader_;

  explicit FastSlideReaderWrapper(
      std::shared_ptr<fastslide::SlideReader> reader)
      : reader_(std::move(reader)) {}
};

}  // namespace fastslide::wasm

// Emscripten bindings
EMSCRIPTEN_BINDINGS(fastslide) {
  using namespace fastslide::wasm;

  emscripten::class_<FastSlideLevelInfo>("FastSlideLevelInfo")
      .property("width", &FastSlideLevelInfo::width)
      .property("height", &FastSlideLevelInfo::height)
      .property("downsampleFactor", &FastSlideLevelInfo::downsampleFactor);

  emscripten::class_<FastSlideReaderWrapper>("FastSlideReader")
      .class_function("fromFilePath", &FastSlideReaderWrapper::fromFilePath)
      .property("numLevels", &FastSlideReaderWrapper::numLevels)
      .property("dimensions", &FastSlideReaderWrapper::dimensions)
      .property("format", &FastSlideReaderWrapper::format)
      .property("mpp", &FastSlideReaderWrapper::mpp)
      .function("getLevelInfo", &FastSlideReaderWrapper::getLevelInfo)
      .function("getLevelDimensions",
                &FastSlideReaderWrapper::getLevelDimensions)
      .function("readRegion", &FastSlideReaderWrapper::readRegion);
}
