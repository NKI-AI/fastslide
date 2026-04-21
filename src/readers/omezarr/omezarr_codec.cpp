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

#include "fastslide/readers/omezarr/omezarr_codec.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "simpletiff/deflate.h"
#include "simpletiff/zstd.h"

namespace fastslide::formats::omezarr {

namespace {

using json = nlohmann::json;

aifocore::Status MakeError(std::string message) {
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                              std::move(message));
}

aifocore::Status MakeUnsupported(std::string message) {
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                              std::move(message));
}

bool IsTrivialTranspose(const json& cfg, size_t rank) {
  if (!cfg.is_object())
    return true;
  if (!cfg.contains("order"))
    return true;
  const auto& order = cfg["order"];
  if (order.is_string()) {
    const auto s = order.get<std::string>();
    return s == "C" || s == "c";
  }
  if (order.is_array()) {
    if (order.size() != rank)
      return false;
    for (size_t i = 0; i < rank; ++i) {
      if (!order[i].is_number_integer())
        return false;
      if (order[i].get<int64_t>() != static_cast<int64_t>(i))
        return false;
    }
    return true;
  }
  return true;
}

void ByteSwapInPlace(std::vector<uint8_t>& buffer, uint32_t element_bytes) {
  if (element_bytes <= 1)
    return;
  const size_t n = buffer.size() / element_bytes;
  uint8_t* data = buffer.data();
  for (size_t i = 0; i < n; ++i) {
    uint8_t* element = data + i * element_bytes;
    for (uint32_t j = 0; j < element_bytes / 2; ++j) {
      std::swap(element[j], element[element_bytes - 1 - j]);
    }
  }
}

aifocore::Status DecodeZstd(std::vector<uint8_t>* buffer,
                            size_t expected_bytes) {
  std::vector<uint8_t> output;
  if (auto s = simpletiff::DecompressZstd(*buffer, output); !s.ok()) {
    return MakeError(aifocore::fmt::format("zstd decompression failed: {}",
                                           s.status().message()));
  }
  if (output.size() != expected_bytes) {
    return MakeError(aifocore::fmt::format(
        "zstd decompressed size mismatch: expected {}, got {}", expected_bytes,
        output.size()));
  }
  *buffer = std::move(output);
  return aifocore::Status::OkStatus();
}

aifocore::Status DecodeGzip(std::vector<uint8_t>* buffer,
                            size_t expected_bytes) {
  std::vector<uint8_t> output;
  if (auto s = simpletiff::DecompressDeflate(*buffer, output); !s.ok()) {
    return MakeError(aifocore::fmt::format(
        "gzip/deflate decompression failed: {}", s.status().message()));
  }
  if (output.size() != expected_bytes) {
    return MakeError(aifocore::fmt::format(
        "gzip decompressed size mismatch: expected {}, got {}", expected_bytes,
        output.size()));
  }
  *buffer = std::move(output);
  return aifocore::Status::OkStatus();
}

}  // namespace

aifocore::Result<ZarrCodecChain> ZarrCodecChain::Build(
    const std::vector<ZarrCodec>& codecs,
    const std::vector<uint64_t>& chunk_shape, ZarrDtype dtype) {
  ZarrCodecChain chain;
  chain.element_bytes_ = dtype.BytesPerElement();
  chain.expected_size_bytes_ = chain.element_bytes_;
  for (uint64_t dim : chunk_shape) {
    if (dim == 0) {
      return MakeError("Zarr chunk shape contains zero-sized dimension");
    }
    chain.expected_size_bytes_ *= static_cast<size_t>(dim);
  }
  chain.stages_.reserve(codecs.size());
  for (const auto& codec : codecs) {
    json cfg;
    try {
      cfg = json::parse(codec.configuration.empty() ? std::string{"{}"}
                                                    : codec.configuration);
    } catch (const json::parse_error&) {
      cfg = json::object();
    }
    if (codec.name == "bytes" || codec.name == "endian") {
      chain.stages_.push_back(Stage::kBytes);
      if (cfg.is_object() && cfg.contains("endian") &&
          cfg["endian"].is_string()) {
        const auto e = cfg["endian"].get<std::string>();
        chain.endian_ = (e == "big") ? Endian::kBig : Endian::kLittle;
      }
    } else if (codec.name == "transpose") {
      if (!IsTrivialTranspose(cfg, chunk_shape.size())) {
        return MakeUnsupported(
            "OME-Zarr 'transpose' codec with non-trivial order is not "
            "supported");
      }
      chain.stages_.push_back(Stage::kTranspose);
    } else if (codec.name == "zstd") {
      chain.stages_.push_back(Stage::kZstd);
    } else if (codec.name == "gzip") {
      chain.stages_.push_back(Stage::kGzip);
    } else if (codec.name == "blosc" || codec.name == "blosc2") {
      return MakeUnsupported(
          "OME-Zarr 'blosc' codec is not yet supported by this build");
    } else {
      return MakeUnsupported(aifocore::fmt::format(
          "OME-Zarr codec '{}' is not supported", codec.name));
    }
  }
  return chain;
}

aifocore::Result<std::vector<uint8_t>> ZarrCodecChain::Decode(
    std::span<const uint8_t> compressed) const {
  std::vector<uint8_t> buffer(compressed.begin(), compressed.end());
  for (auto it = stages_.rbegin(); it != stages_.rend(); ++it) {
    switch (*it) {
      case Stage::kBytes:
        if (endian_ == Endian::kBig &&
            std::endian::native == std::endian::little) {
          ByteSwapInPlace(buffer, element_bytes_);
        } else if (endian_ == Endian::kLittle &&
                   std::endian::native == std::endian::big) {
          ByteSwapInPlace(buffer, element_bytes_);
        }
        break;
      case Stage::kTranspose:
        // Trivial / no-op identity transpose validated at build time.
        break;
      case Stage::kZstd:
        AIFOCORE_RETURN_IF_ERROR(DecodeZstd(&buffer, expected_size_bytes_));
        break;
      case Stage::kGzip:
        AIFOCORE_RETURN_IF_ERROR(DecodeGzip(&buffer, expected_size_bytes_));
        break;
      case Stage::kBlosc:
        return MakeUnsupported(
            "OME-Zarr 'blosc' codec is not yet supported by this build");
    }
  }
  if (buffer.size() != expected_size_bytes_) {
    return MakeError(aifocore::fmt::format(
        "OME-Zarr codec chain produced {} bytes, expected {}", buffer.size(),
        expected_size_bytes_));
  }
  return buffer;
}

}  // namespace fastslide::formats::omezarr
