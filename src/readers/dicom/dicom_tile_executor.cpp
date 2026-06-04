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

#include "fastslide/readers/dicom/dicom_tile_executor.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <dicom/dicom.h>
}

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/dicom/dicom_decode.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

namespace {

/// @brief Read a raw frame from the DICOM file and return the bytes + syntax.
///
/// The mutex is held only for the libdicom I/O (which is not thread-safe).
/// Callers decode the returned payload outside the lock.
struct RawFrame {
  std::vector<uint8_t> bytes;
  DicomTransferSyntax syntax;
  DicomPhotometric photometric;
};

aifocore::Result<RawFrame> ReadRawFrame(DicomFile& file, uint32_t col,
                                        uint32_t row) {
  std::lock_guard<std::mutex> lock(file.mutex);

  DcmError* dcm_error = nullptr;
  UniqueDcmFrame frame(dcm_filehandle_read_frame_position(
      &dcm_error, file.filehandle.get(), col, row));

  if (!frame) {
    if (dcm_error &&
        dcm_error_get_code(dcm_error) == DCM_ERROR_CODE_MISSING_FRAME) {
      dcm_error_clear(&dcm_error);
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                  "Missing DICOM frame");
    }
    std::string msg = "DICOM frame read error";
    if (dcm_error) {
      const char* summary = dcm_error_get_summary(dcm_error);
      if (summary)
        msg = summary;
      dcm_error_clear(&dcm_error);
    }
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal, msg);
  }

  const auto* raw =
      reinterpret_cast<const uint8_t*>(dcm_frame_get_value(frame.get()));
  uint32_t length = dcm_frame_get_length(frame.get());

  return RawFrame{std::vector<uint8_t>(raw, raw + length), file.transfer_syntax,
                  file.photometric};
}

aifocore::Result<std::vector<uint8_t>> DecodeFrame(DicomFile& file,
                                                   uint32_t col, uint32_t row,
                                                   uint32_t expected_w,
                                                   uint32_t expected_h) {
  AIFOCORE_ASSIGN_OR_RETURN(auto raw_frame, ReadRawFrame(file, col, row));
  return dicom::internal::DecodeDicomFrameBytes(
      raw_frame.bytes, raw_frame.syntax, raw_frame.photometric, expected_w,
      expected_h);
}

}  // namespace

aifocore::Status DicomTileExecutor::ExecutePlan(const core::TilePlan& plan,
                                                const DicomExecContext& context,
                                                runtime::Canvas& writer) {
  if (plan.operations.empty()) {
    const auto& bg = plan.output.background;
    return writer.FillBackground(bg.r, bg.g, bg.b);
  }

  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  std::mutex writer_mutex;
  std::atomic<int> error_count{0};

  auto futures = pool.submit_sequence(0, plan.operations.size(), [&](size_t i) {
    const auto& op = plan.operations[i];
    auto st = ExecuteTileOperation(op, context, writer, writer_mutex);
    if (!st.ok()) {
      const int n = ++error_count;
      if (n <= 10) {
        std::cerr << "DICOM tile op failed: " << st.ToString() << "\n";
      }
    }
  });

  futures.wait();
  return aifocore::Status::OkStatus();
}

runtime::TileKey DicomTileExecutor::MakeCacheKey(
    const core::TileReadOp& op, const DicomExecContext& context) {
  return runtime::TileKey(std::string(context.GetFilename()),
                          static_cast<uint16_t>(op.level), op.tile_coord.x,
                          op.tile_coord.y);
}

aifocore::Result<DecodedTileData> DicomTileExecutor::ReadTileFromDisk(
    const core::TileReadOp& op, const DicomExecContext& context) {
  const auto& level = context.GetLevel(op.level);

  const uint32_t col = op.tile_coord.x;
  const uint32_t row = op.tile_coord.y;

  // Locate the concatenation part holding this tile. We cannot derive it
  // from `concatenation_frame_offset` alone because the offset partitions
  // the *frame stream*, while (col, row) addresses the *tile grid*. In
  // TILED_SPARSE layouts the two indices are unrelated. Instead probe the
  // parts in order: libdicom returns MISSING_FRAME for tiles owned by a
  // sibling, which is cheap (no pixel I/O) once the basic-offset table is
  // cached. For unsegmented levels there is exactly one part.
  std::vector<uint8_t> rgb;
  bool decoded = false;
  for (const auto& part : level.parts) {
    auto rgb_or = DecodeFrame(*part.file, col, row, level.tile_w, level.tile_h);
    if (rgb_or.ok()) {
      rgb = std::move(*rgb_or);
      decoded = true;
      break;
    }
    if (rgb_or.status().code() != aifocore::StatusCode::kNotFound) {
      return rgb_or.status();
    }
  }
  if (!decoded) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        "Missing DICOM frame in all concatenation parts");
  }

  const uint32_t actual_w =
      std::min(level.tile_w, level.width - col * level.tile_w);
  const uint32_t actual_h =
      std::min(level.tile_h, level.height - row * level.tile_h);

  auto& tile_buf = GetBuffers().tile_buffer;

  // DICOM frames are always full tile_w x tile_h, even for edge tiles
  // (per PS3.3 C.7.6.3). Strip the padding so the buffer stride matches
  // the actual pixel extent at the image boundary.
  if (actual_w != level.tile_w || actual_h != level.tile_h) {
    std::vector<uint8_t> cropped(actual_w * actual_h * 3);
    for (uint32_t y = 0; y < actual_h; ++y) {
      std::memcpy(cropped.data() + y * actual_w * 3,
                  rgb.data() + y * level.tile_w * 3, actual_w * 3);
    }
    tile_buf = std::move(cropped);
  } else {
    tile_buf = std::move(rgb);
  }

  return DecodedTileData{
      std::span<const uint8_t>(tile_buf.data(), tile_buf.size()), actual_w,
      actual_h, 3};
}

aifocore::Status DicomTileExecutor::ExecuteTileOperation(
    const core::TileReadOp& op, const DicomExecContext& context,
    runtime::Canvas& writer, std::mutex& writer_mutex) {
  auto tile_data_or = ReadWithCache(op, context);
  if (!tile_data_or.ok()) {
    // Missing frame — leave the background fill (sparse tile grids are valid).
    return aifocore::Status::OkStatus();
  }

  const auto& level = context.GetLevel(op.level);
  const uint32_t col = op.tile_coord.x;
  const uint32_t row = op.tile_coord.y;
  const uint32_t tile_w =
      std::min(level.tile_w, level.width - col * level.tile_w);
  const uint32_t tile_h =
      std::min(level.tile_h, level.height - row * level.tile_h);

  const std::span<const uint8_t> tile_data = *tile_data_or;
  auto st = writer.PaintTile(op, tile_data, tile_w, tile_h, 3, writer_mutex);
  if (!st.ok()) {
    return aifocore::Status::OkStatus();
  }
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide
