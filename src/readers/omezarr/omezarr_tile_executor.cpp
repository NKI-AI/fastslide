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

#include "fastslide/readers/omezarr/omezarr_tile_executor.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/omezarr/omezarr.h"
#include "fastslide/readers/simpletiff_tile_executor_utils.h"
#include "fastslide/runtime/cache_interface.h"

namespace fs = std::filesystem;

namespace fastslide {

namespace {

aifocore::Status MakeIoError(std::string message) {
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                              std::move(message));
}

uint64_t DecodeChunkY(uint64_t encoded) {
  return encoded >> 32;
}

uint64_t DecodeChunkX(uint64_t encoded) {
  return encoded & 0xFFFFFFFFULL;
}

/// @brief Build the on-disk relative chunk path for a Zarr V3 default
/// chunk-key encoding ("c" prefix + per-axis indices).
std::string BuildChunkRelativePath(const OmeZarrLevelInfo& level,
                                   uint64_t chunk_y, uint64_t chunk_x,
                                   uint64_t chunk_c) {
  const char sep = level.array_metadata.chunk_key_separator;
  std::string path = "c";
  const auto rank = level.array_metadata.shape.size();
  for (size_t i = 0; i < rank; ++i) {
    uint64_t idx = 0;
    if (i == level.y_axis) {
      idx = chunk_y;
    } else if (i == level.x_axis) {
      idx = chunk_x;
    } else if (i == level.c_axis) {
      idx = chunk_c;
    } else {
      idx = 0;
    }
    path.push_back(sep);
    path += std::to_string(idx);
  }
  return path;
}

aifocore::Result<std::vector<uint8_t>> ReadFileBytes(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream.is_open()) {
    return MakeIoError(
        aifocore::fmt::format("Cannot open chunk '{}'", path.string()));
  }
  const std::streamsize size = stream.tellg();
  if (size < 0) {
    return MakeIoError(
        aifocore::fmt::format("Cannot stat chunk '{}'", path.string()));
  }
  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  stream.seekg(0, std::ios::beg);
  if (!stream.read(reinterpret_cast<char*>(buffer.data()), size)) {
    return MakeIoError(
        aifocore::fmt::format("Read failed for chunk '{}'", path.string()));
  }
  return buffer;
}

/// @brief Build a fill-value chunk buffer for a missing on-disk chunk.
///
/// Per the Zarr V3 spec, chunks that have not been written to the store
/// implicitly contain the array's `fill_value`. Sparse datasets (e.g. CODEX
/// slides with non-rectangular tissue regions) routinely omit empty chunks,
/// so a missing chunk file must NOT be treated as an I/O error.
std::vector<uint8_t> MakeFillValueChunk(const OmeZarrLevelInfo& level) {
  const size_t total_bytes = level.codec_chain.expected_size_bytes();
  const double fill = level.array_metadata.fill_value;
  if (fill == 0.0) {
    return std::vector<uint8_t>(total_bytes, 0);
  }
  std::vector<uint8_t> buffer(total_bytes, 0);
  const uint32_t element_bytes = level.BytesPerSample();
  if (element_bytes == 0 || total_bytes == 0)
    return buffer;

  std::array<uint8_t, 8> element{};
  switch (level.array_metadata.dtype.kind) {
    case formats::omezarr::ZarrDtypeKind::kUInt: {
      const uint64_t v = (fill < 0.0) ? 0u : static_cast<uint64_t>(fill);
      for (uint32_t i = 0; i < element_bytes && i < element.size(); ++i) {
        element[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
      }
      break;
    }
    case formats::omezarr::ZarrDtypeKind::kInt: {
      const int64_t signed_v = static_cast<int64_t>(fill);
      const uint64_t v = static_cast<uint64_t>(signed_v);
      for (uint32_t i = 0; i < element_bytes && i < element.size(); ++i) {
        element[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
      }
      break;
    }
    case formats::omezarr::ZarrDtypeKind::kFloat: {
      if (element_bytes == 4) {
        const float f = static_cast<float>(fill);
        std::memcpy(element.data(), &f, sizeof(f));
      } else if (element_bytes == 8) {
        std::memcpy(element.data(), &fill, sizeof(fill));
      }
      break;
    }
    case formats::omezarr::ZarrDtypeKind::kBool:
      element[0] = (fill != 0.0) ? 1u : 0u;
      break;
  }
  const size_t element_count = total_bytes / element_bytes;
  for (size_t i = 0; i < element_count; ++i) {
    std::memcpy(buffer.data() + i * element_bytes, element.data(),
                element_bytes);
  }
  return buffer;
}

/// @brief Decoded contiguous chunk in scalar host-endian form.
///
/// Layout matches the original Zarr chunk_shape with axes in the array's
/// declared order. For trivially-transposed chunks (the only form we accept)
/// the `(c, y, x)` slicing is row-major along the last two axes.
struct DecodedChunk {
  std::vector<uint8_t> data;
  uint64_t chunk_y = 0;
  uint64_t chunk_x = 0;
  size_t y_stride_bytes = 0;
  size_t x_stride_bytes = 0;
  size_t channel_plane_bytes = 0;
  uint32_t element_bytes = 1;
};

/// @brief Compute per-axis byte strides for the array shape with the array's
/// declared axis order (C-contiguous).
///
/// The strides we care about are the y/x/c byte strides. Other (degenerate)
/// axes have shape 1 so their stride does not matter.
void ComputeStrides(const OmeZarrLevelInfo& level, DecodedChunk* chunk) {
  const auto& shape = level.array_metadata.chunk_shape;
  const size_t rank = shape.size();
  std::vector<size_t> strides(rank);
  size_t running = 1;
  for (size_t i = rank; i-- > 0;) {
    strides[i] = running;
    running *= shape[i];
  }
  chunk->element_bytes = level.BytesPerSample();
  chunk->y_stride_bytes = strides[level.y_axis] * chunk->element_bytes;
  chunk->x_stride_bytes = strides[level.x_axis] * chunk->element_bytes;
  chunk->channel_plane_bytes =
      (level.c_axis != static_cast<size_t>(-1) ? strides[level.c_axis] : 0) *
      chunk->element_bytes;
}

/// @brief Read + decode a single chunk for a given (cy, cx, channel).
///
/// If the chunk file does not exist on disk, returns a buffer pre-filled with
/// the array's `fill_value` per the Zarr V3 spec. Other I/O or decode errors
/// propagate as failures.
aifocore::Status ReadAndDecodeChunk(const OmeZarrReader& reader,
                                    const OmeZarrLevelInfo& level, uint64_t cy,
                                    uint64_t cx, uint64_t cc,
                                    DecodedChunk* out) {
  const fs::path chunk_path =
      fs::path(level.array_dir) / BuildChunkRelativePath(level, cy, cx, cc);

  std::error_code ec;
  if (!fs::exists(chunk_path, ec) || ec) {
    out->data = MakeFillValueChunk(level);
    out->chunk_y = cy;
    out->chunk_x = cx;
    ComputeStrides(level, out);
    return aifocore::Status::OkStatus();
  }

  AIFOCORE_ASSIGN_OR_RETURN(const auto compressed, ReadFileBytes(chunk_path));
  AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                            level.codec_chain.Decode(std::span<const uint8_t>(
                                compressed.data(), compressed.size())));
  out->data = std::move(decoded);
  out->chunk_y = cy;
  out->chunk_x = cx;
  ComputeStrides(level, out);
  return aifocore::Status::OkStatus();
}

/// @brief Extract a single (channel, full-spatial) plane from a decoded chunk
/// into a contiguous (height, width) byte buffer.
void ExtractChannelPlane(const DecodedChunk& chunk,
                         const OmeZarrLevelInfo& level, size_t source_channel,
                         std::vector<uint8_t>* plane_out) {
  const uint64_t plane_h = std::min<uint64_t>(
      level.chunk_y, level.y_size - chunk.chunk_y * level.chunk_y);
  const uint64_t plane_w = std::min<uint64_t>(
      level.chunk_x, level.x_size - chunk.chunk_x * level.chunk_x);
  const size_t plane_bytes_actual =
      static_cast<size_t>(plane_h) * plane_w * chunk.element_bytes;
  plane_out->assign(plane_bytes_actual, 0);

  const size_t channel_offset = (level.c_axis != static_cast<size_t>(-1))
                                    ? source_channel * chunk.channel_plane_bytes
                                    : 0;
  // Inner loop: for each row, copy `plane_w * element_bytes` bytes from the
  // source row of the channel plane to the destination. The y stride
  // corresponds to one row in the chunk; the x stride is `element_bytes`
  // because x is the trailing axis in OME-NGFF C-order arrays.
  for (uint64_t y = 0; y < plane_h; ++y) {
    const size_t src = channel_offset + y * chunk.y_stride_bytes;
    const size_t dst = static_cast<size_t>(y) * plane_w * chunk.element_bytes;
    std::memcpy(plane_out->data() + dst, chunk.data.data() + src,
                static_cast<size_t>(plane_w) * chunk.element_bytes);
  }
}

}  // namespace

aifocore::Status OmeZarrTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                                  const OmeZarrReader& reader,
                                                  runtime::Canvas& canvas) {
  const int level_index = plan.request.level;
  const auto& pyramid = reader.GetPyramid();
  if (level_index < 0 || static_cast<size_t>(level_index) >= pyramid.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("OME-Zarr: invalid level {}", level_index));
  }
  const OmeZarrLevelInfo& level = pyramid[level_index];
  const auto cache = reader.GetCache();
  const std::string filename = reader.GetFilename();
  const auto& source_channels = plan.output.channel_indices;

  // Pre-compute (output_slot, in_chunk_channel) groupings keyed by
  // channel-chunk index `cc`. The plan emits one op per (cc, cy, cx); the
  // executor uses this map to know which canvas slots to paint after each
  // single decode.
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> slots_by_cc;
  if (level.c_axis != static_cast<size_t>(-1) && level.chunk_c > 0) {
    const uint64_t cc_chunks =
        (level.c_size + level.chunk_c - 1) / level.chunk_c;
    slots_by_cc.resize(cc_chunks);
    for (size_t output_slot = 0; output_slot < source_channels.size();
         ++output_slot) {
      const size_t source_channel = source_channels[output_slot];
      const uint64_t cc = source_channel / level.chunk_c;
      const uint64_t in_chunk = source_channel - cc * level.chunk_c;
      if (cc < slots_by_cc.size()) {
        slots_by_cc[cc].emplace_back(static_cast<uint32_t>(output_slot),
                                     static_cast<uint32_t>(in_chunk));
      }
    }
  } else {
    slots_by_cc.resize(1);
    for (size_t output_slot = 0; output_slot < source_channels.size();
         ++output_slot) {
      slots_by_cc[0].emplace_back(static_cast<uint32_t>(output_slot), 0u);
    }
  }

  return readers::simpletiff_exec::ExecuteOpsWithThreadPoolStopOnError(
      plan, canvas,
      [&](const core::TileReadOp& operation, runtime::Canvas& writer,
          std::mutex& writer_mutex) -> aifocore::Status {
        const uint64_t cy = DecodeChunkY(operation.byte_offset);
        const uint64_t cx = DecodeChunkX(operation.byte_offset);
        const uint64_t cc = operation.source_id;

        // Cache key for the decoded chunk. We pack the channel-chunk index
        // `cc` into the upper 16 bits of `tile_y`; this bounds OME-Zarr to
        // <=65535 channel chunks and <=65535 spatial rows of chunks per
        // level, which is more than sufficient for any realistic dataset.
        const uint32_t encoded_y = (static_cast<uint32_t>(cc) << 16) |
                                   (static_cast<uint32_t>(cy) & 0xFFFFU);
        runtime::TileKey key(filename, static_cast<uint16_t>(level_index),
                             static_cast<uint32_t>(cx), encoded_y);

        DecodedChunk chunk;
        std::shared_ptr<runtime::CachedTileData> cached_full_chunk;
        if (cache) {
          cached_full_chunk = cache->Get(key);
        }
        if (cached_full_chunk) {
          chunk.data = cached_full_chunk->data;
          chunk.chunk_y = cy;
          chunk.chunk_x = cx;
          ComputeStrides(level, &chunk);
        } else {
          AIFOCORE_RETURN_IF_ERROR(
              ReadAndDecodeChunk(reader, level, cy, cx, cc, &chunk));
          if (cache) {
            auto entry = std::make_shared<runtime::CachedTileData>(
                chunk.data,
                aifocore::Size<uint32_t, 2>{
                    static_cast<uint32_t>(level.chunk_x),
                    static_cast<uint32_t>(level.chunk_y)},
                static_cast<uint32_t>(level.chunk_c));
            cache->Put(key, std::move(entry));
          }
        }

        const uint64_t plane_h = std::min<uint64_t>(
            level.chunk_y, level.y_size - cy * level.chunk_y);
        const uint64_t plane_w = std::min<uint64_t>(
            level.chunk_x, level.x_size - cx * level.chunk_x);

        if (cc >= slots_by_cc.size()) {
          // Defensive: plan referenced a cc with no requested output slots.
          return aifocore::Status::OkStatus();
        }

        // Reuse one allocation per worker thread for the per-channel plane.
        thread_local std::vector<uint8_t> plane_buffer;
        for (const auto& [output_slot, in_chunk_channel] : slots_by_cc[cc]) {
          ExtractChannelPlane(chunk, level, in_chunk_channel, &plane_buffer);

          // Synthesize a per-channel paint op: identical to `operation`
          // except for `tile_coord.x`, which selects the output canvas slot
          // (`Canvas::PaintTilePlanar` reads only this field plus transform).
          core::TileReadOp paint_op = operation;
          paint_op.tile_coord.x = output_slot;
          AIFOCORE_RETURN_IF_ERROR(
              readers::simpletiff_exec::PaintTileMaybeLocked(
                  writer, paint_op,
                  std::span<const uint8_t>(plane_buffer.data(),
                                           plane_buffer.size()),
                  static_cast<uint32_t>(plane_w),
                  static_cast<uint32_t>(plane_h),
                  /*tile_channels=*/1, writer_mutex));
        }
        return aifocore::Status::OkStatus();
      });
}

}  // namespace fastslide
