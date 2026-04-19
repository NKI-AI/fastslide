//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE

#include "fastslide/readers/isyntax/third_party/file.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/cache.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/jpeg.h"
#include "fastslide/readers/isyntax/third_party/open.h"
#include "fastslide/readers/isyntax/third_party/reader.h"

namespace isyntax {
namespace {

std::once_flag g_init_once;

void EnsureGlobalInitOnce() {
  // This mirrors the essential bits from the legacy libisyntax_init().
  // Keep using the existing platform/system init routines.
  init_thread_memory(0, &global_system_info);
}

int32_t ToInternalPixelFormat(PixelFormat fmt) {
  switch (fmt) {
    case PixelFormat::kRgba:
      return ISYNTAX_PIXEL_FORMAT_RGBA;
    case PixelFormat::kBgra:
      return ISYNTAX_PIXEL_FORMAT_BGRA;
  }
  return 0;
}

}  // namespace

void IsyntaxFile::IsyntaxDeleter::operator()(isyntax_t* p) const {
  if (p == nullptr) {
    return;
  }
  isyntax::DestroyIsyntax(p);
  std::free(p);
}

IsyntaxFile::IsyntaxFile(std::unique_ptr<isyntax_t, IsyntaxDeleter> isyntax,
                         std::unique_ptr<IsyntaxCache> cache)
    : isyntax_(std::move(isyntax)), cache_(std::move(cache)) {}

IsyntaxFile::~IsyntaxFile() = default;

void IsyntaxFile::EnsureThreadInit() {
  // Mirror the legacy `libisyntax_ensure_thread_init()` behavior without
  // keeping the removed C API around.
  if (!local_thread_memory) {
    init_thread_memory(0, &global_system_info);
  }
}

aifocore::Result<std::unique_ptr<IsyntaxFile>> IsyntaxFile::Open(
    std::string_view filename, bool dump_xml_header) {
  std::call_once(g_init_once, &EnsureGlobalInitOnce);

  auto* raw = static_cast<isyntax_t*>(std::malloc(sizeof(isyntax_t)));
  if (raw == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                            "Failed to allocate isyntax_t");
  }
  std::memset(raw, 0, sizeof(isyntax_t));
  std::unique_ptr<isyntax_t, IsyntaxDeleter> handle(raw);

  const isyntax_open_flags_t flags =
      dump_xml_header
          ? static_cast<isyntax_open_flags_t>(ISYNTAX_OPEN_FLAG_DUMP_XML_HEADER)
          : static_cast<isyntax_open_flags_t>(0);

  std::string filename_str(filename);
  aifocore::Status st =
      isyntax::OpenIsyntaxFile(handle.get(), filename_str.c_str(), flags);
  if (!st.ok()) {
    return st;
  }

  // Create a coefficient cache. This is required by isyntax_reader.c tile path.
  // Keep size modest; fastslide has an outer RGB cache.
  constexpr int32_t kDefaultCacheTiles = 500;
  auto cache_or = IsyntaxCache::CreateAndInject(
      "IsyntaxFileCache", kDefaultCacheTiles, handle.get());
  if (!cache_or.ok()) {
    return cache_or.status();
  }

  return std::unique_ptr<IsyntaxFile>(
      new IsyntaxFile(std::move(handle), std::move(*cache_or)));
}

aifocore::Status IsyntaxFile::ReadTile(int32_t level, int64_t tile_x,
                                       int64_t tile_y,
                                       std::span<uint32_t> out_pixels,
                                       PixelFormat pixel_format) const {
  if (isyntax_ == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "ReadTile called on null IsyntaxFile");
  }
  if (cache_ == nullptr || cache_->get() == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "ReadTile called without initialized cache");
  }

  const int32_t tile_w = tile_width();
  const int32_t tile_h = tile_height();
  if (tile_w <= 0 || tile_h <= 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Invalid tile dimensions");
  }
  const int64_t needed =
      static_cast<int64_t>(tile_w) * static_cast<int64_t>(tile_h);
  if (static_cast<int64_t>(out_pixels.size()) < needed) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Output buffer too small");
  }

  const int32_t internal_fmt = ToInternalPixelFormat(pixel_format);
  if (internal_fmt == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid pixel format");
  }

  return isyntax::TileRead(isyntax_.get(), cache_->get(), level,
                           static_cast<int>(tile_x), static_cast<int>(tile_y),
                           out_pixels.data(),
                           static_cast<isyntax_pixel_format_t>(internal_fmt));
}

int32_t IsyntaxFile::tile_width() const {
  if (isyntax_ == nullptr) {
    return 0;
  }
  return isyntax_->tile_width;
}

int32_t IsyntaxFile::tile_height() const {
  if (isyntax_ == nullptr) {
    return 0;
  }
  return isyntax_->tile_height;
}

int32_t IsyntaxFile::level_count() const {
  if (isyntax_ == nullptr) {
    return 0;
  }
  const isyntax_image_t* wsi = isyntax_->images + isyntax_->wsi_image_index;
  return wsi ? wsi->level_count : 0;
}

const isyntax_level_t* IsyntaxFile::level(int32_t idx) const {
  if (isyntax_ == nullptr) {
    return nullptr;
  }
  const isyntax_image_t* wsi = isyntax_->images + isyntax_->wsi_image_index;
  if (wsi == nullptr || idx < 0 || idx >= wsi->level_count) {
    return nullptr;
  }
  return &wsi->levels[idx];
}

const char* IsyntaxFile::barcode() const {
  if (isyntax_ == nullptr) {
    return nullptr;
  }
  return isyntax_->barcode;
}

isyntax_cache_t* IsyntaxFile::cache() const {
  return cache_ ? cache_->get() : nullptr;
}

aifocore::Result<RgbaImage> IsyntaxFile::ReadLabelImage(
    PixelFormat pixel_format) const {
  if (isyntax_ == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "ReadLabelImage called on null IsyntaxFile");
  }
  const isyntax_image_t* img = isyntax_->images + isyntax_->label_image_index;
  if (img == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kNotFound, "No label image");
  }

  const int32_t internal_fmt = ToInternalPixelFormat(pixel_format);
  if (internal_fmt == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid pixel format");
  }

  auto decoded_or = isyntax::jpeg::ReadAssociatedImagePixels(
      isyntax_.get(), const_cast<isyntax_image_t*>(img),
      static_cast<isyntax_pixel_format_t>(internal_fmt));
  if (!decoded_or.ok()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to decode label image");
  }

  const auto& decoded = *decoded_or;
  const int32_t w = decoded.width;
  const int32_t h = decoded.height;
  if (w <= 0 || h <= 0 || decoded.pixels.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Label image decode returned invalid dimensions");
  }
  RgbaImage out;
  out.width = w;
  out.height = h;
  out.pixels = decoded.pixels;
  return out;
}

aifocore::Result<RgbaImage> IsyntaxFile::ReadMacroImage(
    PixelFormat pixel_format) const {
  if (isyntax_ == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "ReadMacroImage called on null IsyntaxFile");
  }
  const isyntax_image_t* img = isyntax_->images + isyntax_->macro_image_index;
  if (img == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kNotFound, "No macro image");
  }

  const int32_t internal_fmt = ToInternalPixelFormat(pixel_format);
  if (internal_fmt == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid pixel format");
  }

  auto decoded_or = isyntax::jpeg::ReadAssociatedImagePixels(
      isyntax_.get(), const_cast<isyntax_image_t*>(img),
      static_cast<isyntax_pixel_format_t>(internal_fmt));
  if (!decoded_or.ok()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to decode macro image");
  }

  const auto& decoded = *decoded_or;
  const int32_t w = decoded.width;
  const int32_t h = decoded.height;
  if (w <= 0 || h <= 0 || decoded.pixels.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Macro image decode returned invalid dimensions");
  }
  RgbaImage out;
  out.width = w;
  out.height = h;
  out.pixels = decoded.pixels;
  return out;
}

aifocore::Result<JpegBuffer> IsyntaxFile::ReadLabelImageJpeg() const {
  if (isyntax_ == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "ReadLabelImageJpeg called on null IsyntaxFile");
  }
  isyntax_image_t* img = isyntax_->images + isyntax_->label_image_index;
  if (img == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kNotFound, "No label image");
  }
  auto bytes_or =
      isyntax::jpeg::ReadAssociatedImageJpegBytes(isyntax_.get(), img);
  if (!bytes_or.ok() || bytes_or->empty()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to read label JPEG");
  }
  JpegBuffer out;
  out.bytes = std::move(*bytes_or);
  return out;
}

aifocore::Result<JpegBuffer> IsyntaxFile::ReadMacroImageJpeg() const {
  if (isyntax_ == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "ReadMacroImageJpeg called on null IsyntaxFile");
  }
  isyntax_image_t* img = isyntax_->images + isyntax_->macro_image_index;
  if (img == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kNotFound, "No macro image");
  }
  auto bytes_or =
      isyntax::jpeg::ReadAssociatedImageJpegBytes(isyntax_.get(), img);
  if (!bytes_or.ok() || bytes_or->empty()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to read macro JPEG");
  }
  JpegBuffer out;
  out.bytes = std::move(*bytes_or);
  return out;
}

aifocore::Result<IccProfile> IsyntaxFile::ReadIccProfileForWsi() const {
  if (isyntax_ == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kFailedPrecondition,
                            "ReadIccProfileForWsi called on null IsyntaxFile");
  }
  isyntax_image_t* img = isyntax_->images + isyntax_->wsi_image_index;
  if (img == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kNotFound, "No WSI image");
  }
  auto bytes_or = isyntax::jpeg::ReadIccProfileBytes(isyntax_.get(), img);
  if (!bytes_or.ok() || bytes_or->empty()) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to read ICC profile");
  }
  IccProfile out;
  out.bytes = std::move(*bytes_or);
  return out;
}

}  // namespace isyntax
