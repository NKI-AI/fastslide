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

#include "fastslide/readers/ndpitiff/ndpitiff.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/ndpitiff/ndpitiff_exec_context.h"
#include "fastslide/readers/ndpitiff/ndpitiff_jpeg_header.h"
#include "fastslide/readers/ndpitiff/ndpitiff_plan_builder.h"
#include "fastslide/readers/ndpitiff/ndpitiff_plan_context.h"
#include "fastslide/readers/ndpitiff/ndpitiff_tile_executor.h"
#include "simpletiff/index.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_parser.h"

namespace fs = std::filesystem;

namespace fastslide {
namespace {

constexpr uint16_t kCompressionJpeg = 7;
constexpr uint16_t kPhotometricYCbCr = 6;

// JPEG SOF markers (baseline/progressive/etc).
[[nodiscard]] bool IsSofMarker(uint8_t marker) {
  switch (marker) {
    case 0xC0:
    case 0xC1:
    case 0xC2:
    case 0xC3:
    case 0xC5:
    case 0xC6:
    case 0xC7:
    case 0xC9:
    case 0xCA:
    case 0xCB:
    case 0xCD:
    case 0xCE:
    case 0xCF:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] uint16_t ReadBigEndianU16(const uint8_t* ptr) {
  return static_cast<uint16_t>(static_cast<uint16_t>(ptr[0]) << 8U |
                               static_cast<uint16_t>(ptr[1]));
}

[[nodiscard]] bool LooksLikeJpegStream(std::span<const uint8_t> bytes) {
  return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
}

[[nodiscard]] uint32_t ComputeEdgeTileDim(uint32_t nominal, uint32_t offset,
                                          uint32_t full) {
  if (offset >= full) {
    return 0;
  }
  return std::min<uint32_t>(nominal, full - offset);
}

[[nodiscard]] const char* GetEnvOrNull(const char* name) {
  return std::getenv(name);
}

[[nodiscard]] bool IsNdpiDebugEnabled() {
  const char* v = GetEnvOrNull("FASTSLIDE_NDPI_DEBUG");
  return v != nullptr && std::string_view(v) == "1";
}

[[nodiscard]] const char* StorageName(simpletiff::Storage storage) {
  switch (storage) {
    case simpletiff::Storage::kTiles:
      return "tiles";
    case simpletiff::Storage::kStrips:
      return "strips";
    case simpletiff::Storage::kSingleJpeg:
      return "single_jpeg";
    default:
      return "unknown";
  }
}

// Validates that an NDPI associated image page (macro/map) has plausible
// parameters and can be decoded by ReadAssociatedImage.
//
// Some NDPI files contain map pages that omit required TIFF tags such as
// SamplesPerPixel. Such pages cannot be decoded through the generic simpletiff
// path (which requires samples_per_pixel * bytes_per_sample > 0). For
// JPEG-compressed pages the actual sample count is recovered from the JPEG SOF
// marker by the NDPI reader, so a missing SamplesPerPixel tag is tolerated.
[[nodiscard]] bool IsAssociatedImagePageDecodable(
    const simpletiff::TiffIndex& tiff_index, uint16_t page_index) {
  if (page_index >= tiff_index.NumPages()) {
    return false;
  }
  const auto& page = tiff_index.Page(page_index);
  if (page.width == 0 || page.height == 0) {
    return false;
  }
  if (page.storage == simpletiff::Storage::kUnknown) {
    return false;
  }
  if (page.compression == kCompressionJpeg &&
      (page.storage == simpletiff::Storage::kTiles ||
       page.storage == simpletiff::Storage::kStrips)) {
    return true;
  }
  return page.samples_per_pixel > 0 && page.bits_per_sample > 0;
}

void MaybeDumpJpegStreamOnce(std::span<const uint8_t> jpeg_stream) {
  static bool dumped = false;
  if (dumped) {
    return;
  }
  const char* path = GetEnvOrNull("FASTSLIDE_NDPI_DUMP_JPEG_STREAM");
  if (path == nullptr || std::strlen(path) == 0) {
    return;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return;
  }
  out.write(reinterpret_cast<const char*>(jpeg_stream.data()),
            static_cast<std::streamsize>(jpeg_stream.size()));
  dumped = true;
}

aifocore::Result<std::vector<uint8_t>> FindNdpiJpegHeaderTemplate(
    const std::vector<uint8_t>& file_prefix) {
  // NDPI typically stores a "template" JPEG header (SOI..SOS) near the start of
  // the file. Tiles store headerless scan payloads which must be prefixed with
  // this header and suffixed with EOI.
  size_t scan_pos = 0;
  while (scan_pos + 4 < file_prefix.size()) {
    size_t soi_index = std::string::npos;
    for (size_t i = scan_pos; i + 1 < file_prefix.size(); ++i) {
      if (file_prefix[i] == 0xFF && file_prefix[i + 1] == 0xD8) {
        soi_index = i;
        break;
      }
    }
    if (soi_index == std::string::npos) {
      break;
    }
    if (soi_index + 4 >= file_prefix.size()) {
      break;
    }

    // Require the pattern FF D8 FF xx where xx is a non-zero marker.
    if (file_prefix[soi_index + 2] != 0xFF) {
      scan_pos = soi_index + 1;
      continue;
    }
    const uint8_t next_marker = file_prefix[soi_index + 3];
    if (next_marker == 0x00 || next_marker == 0xFF) {
      scan_pos = soi_index + 1;
      continue;
    }

    // Find Start of Scan (SOS, FF DA). We want to include the SOS segment
    // header and stop right before scan data.
    size_t sos_index = soi_index;
    for (; sos_index + 1 < file_prefix.size(); ++sos_index) {
      if (file_prefix[sos_index] == 0xFF &&
          file_prefix[sos_index + 1] == 0xDA) {
        break;
      }
    }
    if (sos_index + 4 >= file_prefix.size()) {
      scan_pos = soi_index + 1;
      continue;
    }

    const uint16_t sos_len = ReadBigEndianU16(&file_prefix[sos_index + 2]);
    const size_t header_end = sos_index + 2 + static_cast<size_t>(sos_len);
    if (header_end > file_prefix.size() || header_end <= soi_index) {
      scan_pos = soi_index + 1;
      continue;
    }

    return std::vector<uint8_t>(
        file_prefix.begin() + static_cast<ptrdiff_t>(soi_index),
        file_prefix.begin() + static_cast<ptrdiff_t>(header_end));
  }

  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      "Could not locate NDPI JPEG header template (SOI..SOS) in file prefix");
}

aifocore::Result<std::pair<std::vector<size_t>, std::vector<size_t>>>
FindSofDimensionOffsets(std::span<const uint8_t> header_template) {
  if (header_template.size() < 4) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "JPEG header template too small");
  }
  if (!(header_template[0] == 0xFF && header_template[1] == 0xD8)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "JPEG header template does not start with SOI");
  }

  std::vector<size_t> h_offsets;
  std::vector<size_t> w_offsets;

  // Position after SOI.
  size_t pos = 2;
  while (pos < header_template.size()) {
    // Skip 0xFF padding.
    while (pos < header_template.size() && header_template[pos] == 0xFF) {
      ++pos;
    }
    if (pos >= header_template.size()) {
      break;
    }

    const uint8_t marker = header_template[pos];
    ++pos;

    if (marker == 0x00) {
      continue;
    }
    if (marker == 0xD9) {  // EOI
      break;
    }
    if (marker == 0xDA) {  // SOS - header ends here
      break;
    }
    if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
      // RSTm / TEM have no length.
      continue;
    }
    if (pos + 2 > header_template.size()) {
      break;
    }

    const uint16_t seg_len = ReadBigEndianU16(&header_template[pos]);
    if (seg_len < 2) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Invalid JPEG segment length");
    }

    if (IsSofMarker(marker)) {
      // pos points to length field. SOF payload is:
      // [len_hi len_lo][precision][height_hi height_lo][width_hi width_lo]...
      const size_t sof_h_ptr = pos + 3;
      const size_t sof_w_ptr = pos + 5;
      if (sof_w_ptr + 1 < header_template.size()) {
        h_offsets.push_back(sof_h_ptr);
        w_offsets.push_back(sof_w_ptr);
      }
    }

    pos += static_cast<size_t>(seg_len);
  }

  if (h_offsets.empty() || h_offsets.size() != w_offsets.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        "No SOF markers found in NDPI JPEG header template");
  }

  return std::make_pair(std::move(h_offsets), std::move(w_offsets));
}

}  // namespace

aifocore::Result<std::unique_ptr<NdpiTiffReader>> NdpiTiffReader::Create(
    std::string_view filename) {
  return CreateImpl(filename);
}

NdpiTiffReader::NdpiTiffReader(std::string_view filename)
    : TiffBasedReader(fs::path(filename)) {
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd_val = -1;
  (void)simpletiff::OpenTiff(std::string(filename), *tiff_index_, fd_val);
}

int NdpiTiffReader::GetLevelCount() const {
  return static_cast<int>(pyramid_levels_.size());
}

aifocore::Result<LevelInfo> NdpiTiffReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= static_cast<int>(pyramid_levels_.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }
  const auto& tiff_level = pyramid_levels_[level];
  LevelInfo info;
  info.dimensions = {tiff_level.size[0], tiff_level.size[1]};
  info.downsample_factor = tiff_level.downsample_factor;
  return info;
}

const SlideProperties& NdpiTiffReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> NdpiTiffReader::GetChannelMetadata() const {
  std::vector<ChannelMetadata> metadata;
  metadata.emplace_back("RGB", "Color", ColorRGB{255, 255, 255});
  return metadata;
}

std::vector<std::string> NdpiTiffReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto& img : associated_images_) {
    names.push_back(img.name);
  }
  return names;
}

aifocore::Result<ImageDimensions> NdpiTiffReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      return ImageDimensions{img.size[0], img.size[1]};
    }
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image '{}' not found", name));
}

aifocore::Result<RGBImage> NdpiTiffReader::ReadAssociatedImage(
    std::string_view name) const {
  const NdpiTiffAssociatedInfo* info = nullptr;
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      info = &img;
      break;
    }
  }
  if (info == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image '{}' not found", name));
  }
  if (!tiff_index_ || info->page >= tiff_index_->NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Invalid page {} for associated image '{}'",
                              info->page, name));
  }

  const auto& page_header = tiff_index_->Page(info->page);
  const uint32_t width = info->size[0];
  const uint32_t height = info->size[1];

  // Output is always RGB8 for associated images.
  RGBImage rgb_image({width, height}, ImageFormat::kRGB, DataType::kUInt8);
  uint8_t* dst = rgb_image.GetData();
  const int dst_stride = static_cast<int>(width) * 3;

  // NDPI JPEG pages (macro/map) can be stored as "headerless" JPEG payloads.
  // Passing those bytes directly to jpgd can crash. Reconstruct a full JPEG
  // stream first (SOI..SOS header + scan payload + EOI).
  if (page_header.compression == kCompressionJpeg &&
      (page_header.storage == simpletiff::Storage::kTiles ||
       page_header.storage == simpletiff::Storage::kStrips)) {
    static thread_local simpletiff::DecodeContext decode_ctx;
    static thread_local std::vector<uint8_t> raw_compressed;
    static thread_local std::vector<uint8_t> patched_header;
    static thread_local std::vector<uint8_t> decoded_rgb;

    const simpletiff::JpegDecodeOptions jpeg_options = {
        .treat_ycbcr_as_rgb = (page_header.photometric != kPhotometricYCbCr),
    };

    if (page_header.storage == simpletiff::Storage::kTiles) {
      const auto& tiles = tiff_index_->Tiles(page_header.payload_id);
      if (tiles.tiles_x == 0 || tiles.tiles_y == 0 || tiles.tile_w == 0 ||
          tiles.tile_h == 0) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                    "Invalid NDPI associated tiled geometry");
      }

      for (uint32_t ty = 0; ty < tiles.tiles_y; ++ty) {
        for (uint32_t tx = 0; tx < tiles.tiles_x; ++tx) {
          const uint32_t tile_index = ty * tiles.tiles_x + tx;
          auto rr = simpletiff::ReadRawTile(*tiff_index_, info->page,
                                            tile_index, raw_compressed);
          if (!rr.ok()) {
            return AIFOCORE_MAKE_STATUS(
                aifocore::StatusCode::kInternal,
                aifocore::fmt::format(
                    "Failed to read raw associated tile {}: {}", tile_index,
                    rr.error().message()));
          }

          const uint32_t px = tx * tiles.tile_w;
          const uint32_t py = ty * tiles.tile_h;
          const uint32_t actual_w = ComputeEdgeTileDim(tiles.tile_w, px, width);
          const uint32_t actual_h =
              ComputeEdgeTileDim(tiles.tile_h, py, height);
          if (actual_w == 0 || actual_h == 0 ||
              actual_w > std::numeric_limits<uint16_t>::max() ||
              actual_h > std::numeric_limits<uint16_t>::max()) {
            continue;
          }

          const auto raw_span = std::span<const uint8_t>(raw_compressed.data(),
                                                         raw_compressed.size());

          std::span<const uint8_t> jpeg_stream_span;
          if (LooksLikeJpegStream(raw_span)) {
            jpeg_stream_span = raw_span;
          } else {
            AIFOCORE_RETURN_IF_ERROR(BuildPatchedJpegHeader(
                static_cast<uint16_t>(actual_w),
                static_cast<uint16_t>(actual_h), patched_header));

            decode_ctx.jpeg_stream_buffer.resize(patched_header.size() +
                                                 raw_compressed.size() + 2);
            uint8_t* out_ptr = decode_ctx.jpeg_stream_buffer.data();
            std::memcpy(out_ptr, patched_header.data(), patched_header.size());
            out_ptr += patched_header.size();
            if (!raw_compressed.empty()) {
              std::memcpy(out_ptr, raw_compressed.data(),
                          raw_compressed.size());
              out_ptr += raw_compressed.size();
            }
            out_ptr[0] = 0xFF;
            out_ptr[1] = 0xD9;
            jpeg_stream_span =
                std::span<const uint8_t>(decode_ctx.jpeg_stream_buffer.data(),
                                         decode_ctx.jpeg_stream_buffer.size());
          }

          MaybeDumpJpegStreamOnce(jpeg_stream_span);

          int decoded_w = 0;
          int decoded_h = 0;
          if (!simpletiff::DecodeJpeg(decode_ctx, jpeg_stream_span, decoded_w,
                                      decoded_h, decoded_rgb, jpeg_options)) {
            return AIFOCORE_MAKE_STATUS(
                aifocore::StatusCode::kInternal,
                aifocore::fmt::format(
                    "Failed to decode NDPI associated tile JPEG (tile {})",
                    tile_index));
          }

          // Copy decoded tile into destination (RGB8, contiguous).
          for (int row = 0; row < decoded_h; ++row) {
            const uint32_t dy = py + static_cast<uint32_t>(row);
            if (dy >= height) {
              break;
            }
            const size_t copy_w = std::min<uint32_t>(
                static_cast<uint32_t>(decoded_w), width - px);
            if (copy_w == 0) {
              continue;
            }
            const uint8_t* src_row =
                decoded_rgb.data() +
                static_cast<size_t>(row) * static_cast<size_t>(decoded_w) * 3;
            uint8_t* dst_row =
                dst +
                static_cast<size_t>(dy) * static_cast<size_t>(dst_stride) +
                static_cast<size_t>(px) * 3;
            std::memcpy(dst_row, src_row, copy_w * 3);
          }
        }
      }

      return rgb_image;
    }

    // Strips: decode each strip and stitch.
    const auto& strips = tiff_index_->Strips(page_header.payload_id);
    uint32_t rows_per_strip = strips.rows_per_strip;
    if (rows_per_strip == 0) {
      rows_per_strip = height;
    }
    const uint32_t num_strips = strips.offsets.count;
    uint32_t y_off = 0;

    for (uint32_t strip_index = 0; strip_index < num_strips && y_off < height;
         ++strip_index) {
      const uint32_t strip_h =
          std::min<uint32_t>(rows_per_strip, height - y_off);

      auto rr = simpletiff::ReadRawTile(*tiff_index_, info->page, strip_index,
                                        raw_compressed);
      if (!rr.ok()) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            aifocore::fmt::format("Failed to read raw associated strip {}: {}",
                                  strip_index, rr.error().message()));
      }

      const auto raw_span = std::span<const uint8_t>(raw_compressed.data(),
                                                     raw_compressed.size());
      std::span<const uint8_t> jpeg_stream_span;
      if (LooksLikeJpegStream(raw_span)) {
        jpeg_stream_span = raw_span;
      } else {
        AIFOCORE_RETURN_IF_ERROR(BuildPatchedJpegHeader(
            static_cast<uint16_t>(width), static_cast<uint16_t>(strip_h),
            patched_header));
        decode_ctx.jpeg_stream_buffer.resize(patched_header.size() +
                                             raw_compressed.size() + 2);
        uint8_t* out_ptr = decode_ctx.jpeg_stream_buffer.data();
        std::memcpy(out_ptr, patched_header.data(), patched_header.size());
        out_ptr += patched_header.size();
        if (!raw_compressed.empty()) {
          std::memcpy(out_ptr, raw_compressed.data(), raw_compressed.size());
          out_ptr += raw_compressed.size();
        }
        out_ptr[0] = 0xFF;
        out_ptr[1] = 0xD9;
        jpeg_stream_span =
            std::span<const uint8_t>(decode_ctx.jpeg_stream_buffer.data(),
                                     decode_ctx.jpeg_stream_buffer.size());
      }

      MaybeDumpJpegStreamOnce(jpeg_stream_span);

      int decoded_w = 0;
      int decoded_h = 0;
      if (!simpletiff::DecodeJpeg(decode_ctx, jpeg_stream_span, decoded_w,
                                  decoded_h, decoded_rgb, jpeg_options)) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            aifocore::fmt::format(
                "Failed to decode NDPI associated strip JPEG (strip {})",
                strip_index));
      }

      const uint32_t copy_w = std::min<uint32_t>(
          width, static_cast<uint32_t>(std::max(decoded_w, 0)));
      const uint32_t copy_h = std::min<uint32_t>(
          strip_h, static_cast<uint32_t>(std::max(decoded_h, 0)));
      for (uint32_t row = 0; row < copy_h; ++row) {
        const uint8_t* src_row =
            decoded_rgb.data() +
            static_cast<size_t>(row) * static_cast<size_t>(decoded_w) * 3;
        uint8_t* dst_row = dst + static_cast<size_t>(y_off + row) *
                                     static_cast<size_t>(dst_stride);
        std::memcpy(dst_row, src_row, static_cast<size_t>(copy_w) * 3);
      }

      y_off += strip_h;
    }

    return rgb_image;
  }

  // Fallback: use simpletiff directly.
  simpletiff::DecodeContext ctx;
  simpletiff::Roi roi{0, 0, width, height};
  const int stride = static_cast<int>(width) * 3;
  auto result =
      simpletiff::ReadPage(*tiff_index_, info->page, roi, ctx, dst, stride);
  if (!result.ok()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read associated image '{}': {}", name,
                              result.error().message()));
  }
  return rgb_image;
}

Metadata NdpiTiffReader::GetMetadata() const {
  Metadata metadata;
  metadata[std::string(MetadataKeys::kFormat)] = std::string("NDPI");
  metadata[std::string(MetadataKeys::kLevels)] = pyramid_levels_.size();
  metadata[std::string(MetadataKeys::kChannels)] = static_cast<size_t>(3);

  if (tiff_index_ && tiff_index_->NumPages() > 0) {
    const auto& p0 = tiff_index_->Page(0);
    if (!p0.ndpi_metadata.empty()) {
      metadata[std::string("ndpi.Metadata")] = p0.ndpi_metadata;
    }
    if (p0.ndpi_source_lens.has_value()) {
      metadata[std::string("ndpi.SourceLens")] = *p0.ndpi_source_lens;
    }
  }

  if (properties_.mpp[0] > 0) {
    metadata[std::string(MetadataKeys::kMppX)] = properties_.mpp[0];
    metadata[std::string(MetadataKeys::kMppY)] = properties_.mpp[1];
  }
  if (properties_.objective_magnification > 0) {
    metadata[std::string(MetadataKeys::kMagnification)] =
        properties_.objective_magnification;
  }
  return metadata;
}

ImageDimensions NdpiTiffReader::GetTileSize() const {
  if (pyramid_levels_.empty() || !tiff_index_) {
    return ImageDimensions{256, 256};
  }
  const uint16_t page = pyramid_levels_[0].page;
  if (page >= tiff_index_->NumPages()) {
    return ImageDimensions{256, 256};
  }
  const auto& page_header = tiff_index_->Page(page);
  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index_->Tiles(page_header.payload_id);
    return ImageDimensions{tiles.tile_w, tiles.tile_h};
  }
  if (page_header.storage == simpletiff::Storage::kStrips) {
    const auto& strips = tiff_index_->Strips(page_header.payload_id);
    uint32_t rows_per_strip = strips.rows_per_strip;
    if (rows_per_strip == 0) {
      rows_per_strip = page_header.height;
    }
    return ImageDimensions{page_header.width, rows_per_strip};
  }
  return ImageDimensions{256, 256};
}

aifocore::Result<core::TilePlan> NdpiTiffReader::PrepareRequest(
    const core::TileRequest& request) const {
  const NdpiTiffPlanContext context{
      .pyramid_levels = pyramid_levels_,
      .tiff_index = GetTiffIndex(),
  };
  return NdpiTiffPlanBuilder::BuildPlan(request, context);
}

aifocore::Status NdpiTiffReader::ExecutePlan(const core::TilePlan& plan,
                                             runtime::Canvas& writer) const {
  const NdpiTiffExecContext context{
      .tiff_index = GetTiffIndex(),
      .level_count = GetLevelCount(),
      .jpeg_header_template = jpeg_header_template_,
      .sof_height_offsets = sof_height_offsets_,
      .sof_width_offsets = sof_width_offsets_,
  };
  return NdpiTiffTileExecutor::ExecutePlan(plan, context, writer);
}

uint16_t NdpiTiffReader::GetLevel0Page() const {
  if (pyramid_levels_.empty()) {
    return 0;
  }
  return pyramid_levels_[0].page;
}

aifocore::Status NdpiTiffReader::ProcessMetadata() {
  AIFOCORE_RETURN_IF_ERROR(LoadDirectories());

  // Only attempt to load NDPI JPEG header template if any pyramid page is JPEG.
  bool needs_jpeg_template = false;
  if (tiff_index_ && !pyramid_levels_.empty()) {
    for (const auto& lvl : pyramid_levels_) {
      if (lvl.page < tiff_index_->NumPages() &&
          tiff_index_->Page(lvl.page).compression == kCompressionJpeg) {
        needs_jpeg_template = true;
        break;
      }
    }
  }
  if (needs_jpeg_template) {
    AIFOCORE_RETURN_IF_ERROR(LoadJpegHeaderTemplate());

    // Debug hook: dump reconstructed JPEG stream for pyramid tile 0 (level 0).
    // This allows direct comparison with ndpi_python's reconstruction logic.
    //
    // Usage:
    //   FASTSLIDE_NDPI_DUMP_JPEG_STREAM=/tmp/cpp_ndpi_L0_T0.jpg \
    //     bazelisk run //aifo/fastslide:fastslidetool -- info --input ...ndpi
    if (GetEnvOrNull("FASTSLIDE_NDPI_DUMP_JPEG_STREAM") != nullptr &&
        tiff_index_ && !pyramid_levels_.empty()) {
      const uint16_t page0 = pyramid_levels_.front().page;
      if (page0 < tiff_index_->NumPages()) {
        const auto& page_header = tiff_index_->Page(page0);
        if (page_header.storage == simpletiff::Storage::kTiles) {
          const auto& tiles = tiff_index_->Tiles(page_header.payload_id);
          if (tiles.tiles_x > 0 && tiles.tiles_y > 0 && tiles.tile_w > 0 &&
              tiles.tile_h > 0) {
            static thread_local std::vector<uint8_t> raw_tile;
            static thread_local std::vector<uint8_t> patched_header;
            auto rr = simpletiff::ReadRawTile(*tiff_index_, page0,
                                              /*tile_index=*/0, raw_tile);
            if (rr.ok()) {
              const uint32_t actual_w = ComputeEdgeTileDim(
                  tiles.tile_w, /*offset=*/0, page_header.width);
              const uint32_t actual_h = ComputeEdgeTileDim(
                  tiles.tile_h, /*offset=*/0, page_header.height);
              if (actual_w > 0 && actual_h > 0 &&
                  actual_w <= std::numeric_limits<uint16_t>::max() &&
                  actual_h <= std::numeric_limits<uint16_t>::max()) {
                const auto raw_span =
                    std::span<const uint8_t>(raw_tile.data(), raw_tile.size());
                if (LooksLikeJpegStream(raw_span)) {
                  MaybeDumpJpegStreamOnce(raw_span);
                } else if (BuildPatchedJpegHeader(
                               static_cast<uint16_t>(actual_w),
                               static_cast<uint16_t>(actual_h), patched_header)
                               .ok()) {
                  std::vector<uint8_t> stream;
                  stream.resize(patched_header.size() + raw_tile.size() + 2);
                  uint8_t* out_ptr = stream.data();
                  std::memcpy(out_ptr, patched_header.data(),
                              patched_header.size());
                  out_ptr += patched_header.size();
                  if (!raw_tile.empty()) {
                    std::memcpy(out_ptr, raw_tile.data(), raw_tile.size());
                    out_ptr += raw_tile.size();
                  }
                  out_ptr[0] = 0xFF;
                  out_ptr[1] = 0xD9;
                  MaybeDumpJpegStreamOnce(stream);
                }
              }
            }
          }
        }
      }
    }
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status NdpiTiffReader::LoadDirectories() {
  if (!tiff_index_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "TIFF index not initialized");
  }
  if (tiff_index_->NumPages() == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No TIFF pages found");
  }

  pyramid_levels_.clear();
  associated_images_.clear();

  std::vector<uint16_t> pyramid_pages;
  pyramid_pages.reserve(tiff_index_->NumPages());

  std::optional<uint16_t> macro_page;
  std::optional<uint16_t> map_page;

  bool has_source_lens_any = false;
  for (size_t i = 0; i < tiff_index_->NumPages(); ++i) {
    if (tiff_index_->Page(i).ndpi_source_lens.has_value()) {
      has_source_lens_any = true;
      break;
    }
  }

  for (size_t i = 0; i < tiff_index_->NumPages(); ++i) {
    const auto& page = tiff_index_->Page(i);
    if (page.width == 0 || page.height == 0) {
      continue;
    }

    if (has_source_lens_any && page.ndpi_source_lens.has_value()) {
      const double v = *page.ndpi_source_lens;
      if (v == -1.0) {
        macro_page = static_cast<uint16_t>(i);
        continue;
      }
      if (v == -2.0) {
        map_page = static_cast<uint16_t>(i);
        continue;
      }
      // Pyramid levels are typically tiled, but allow strip-based levels too.
      if (page.storage == simpletiff::Storage::kTiles ||
          page.storage == simpletiff::Storage::kStrips) {
        pyramid_pages.push_back(static_cast<uint16_t>(i));
      }
      continue;
    }

    // Fallback heuristic if SourceLens is missing: mimic generic TIFF behavior.
    const bool is_reduced = (page.new_subfile_type & 0x1U) != 0;
    const bool is_level_storage =
        (page.storage == simpletiff::Storage::kTiles ||
         page.storage == simpletiff::Storage::kStrips);
    if (is_level_storage && (i == 0 || is_reduced)) {
      pyramid_pages.push_back(static_cast<uint16_t>(i));
    }
  }

  if (pyramid_pages.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No NDPI pyramid levels found");
  }

  if (IsNdpiDebugEnabled()) {
    std::fprintf(stderr,
                 "[NDPI] Selected %zu candidate pyramid pages (pre-sort)\n",
                 pyramid_pages.size());
    for (const uint16_t page_idx : pyramid_pages) {
      const auto& page = tiff_index_->Page(page_idx);
      std::fprintf(
          stderr,
          "[NDPI] page=%u %ux%u storage=%s compression=%u photometric=%u\n",
          static_cast<unsigned>(page_idx), static_cast<unsigned>(page.width),
          static_cast<unsigned>(page.height), StorageName(page.storage),
          static_cast<unsigned>(page.compression),
          static_cast<unsigned>(page.photometric));
      if (page.storage == simpletiff::Storage::kTiles) {
        const auto& tiles = tiff_index_->Tiles(page.payload_id);
        std::fprintf(stderr, "       tiles: %ux%u tiles_x=%u tiles_y=%u\n",
                     static_cast<unsigned>(tiles.tile_w),
                     static_cast<unsigned>(tiles.tile_h),
                     static_cast<unsigned>(tiles.tiles_x),
                     static_cast<unsigned>(tiles.tiles_y));
      } else if (page.storage == simpletiff::Storage::kStrips) {
        const auto& strips = tiff_index_->Strips(page.payload_id);
        std::fprintf(stderr, "       strips: rows_per_strip=%u count=%u\n",
                     static_cast<unsigned>(strips.rows_per_strip),
                     static_cast<unsigned>(strips.offsets.count));
      }
    }
    std::fflush(stderr);
  }

  std::sort(pyramid_pages.begin(), pyramid_pages.end(),
            [&](uint16_t a, uint16_t b) {
              const auto& pa = tiff_index_->Page(a);
              const auto& pb = tiff_index_->Page(b);
              if (pa.width != pb.width) {
                return pa.width > pb.width;
              }
              return pa.height > pb.height;
            });

  const uint32_t base_w = tiff_index_->Page(pyramid_pages.front()).width;
  const uint32_t base_h = tiff_index_->Page(pyramid_pages.front()).height;

  pyramid_levels_.reserve(pyramid_pages.size());
  for (const uint16_t page_idx : pyramid_pages) {
    const auto& page = tiff_index_->Page(page_idx);
    const double width_ratio =
        static_cast<double>(base_w) /
        static_cast<double>(std::max<uint32_t>(page.width, 1));
    const double height_ratio =
        static_cast<double>(base_h) /
        static_cast<double>(std::max<uint32_t>(page.height, 1));
    const double downsample = (width_ratio + height_ratio) / 2.0;
    pyramid_levels_.push_back({.page = page_idx,
                               .size = {page.width, page.height},
                               .downsample_factor = downsample});
  }

  if (macro_page.has_value()) {
    if (IsAssociatedImagePageDecodable(*tiff_index_, *macro_page)) {
      const auto& p = tiff_index_->Page(*macro_page);
      associated_images_.push_back(
          {.page = *macro_page, .size = {p.width, p.height}, .name = "macro"});
    } else if (IsNdpiDebugEnabled()) {
      const auto& p = tiff_index_->Page(*macro_page);
      std::fprintf(stderr,
                   "[NDPI] Skipping macro page %u: undecodable parameters "
                   "(samples_per_pixel=%u bits_per_sample=%u storage=%s "
                   "compression=%u)\n",
                   static_cast<unsigned>(*macro_page),
                   static_cast<unsigned>(p.samples_per_pixel),
                   static_cast<unsigned>(p.bits_per_sample),
                   StorageName(p.storage),
                   static_cast<unsigned>(p.compression));
    }
  }
  if (map_page.has_value()) {
    if (IsAssociatedImagePageDecodable(*tiff_index_, *map_page)) {
      const auto& p = tiff_index_->Page(*map_page);
      associated_images_.push_back(
          {.page = *map_page, .size = {p.width, p.height}, .name = "map"});
    } else if (IsNdpiDebugEnabled()) {
      const auto& p = tiff_index_->Page(*map_page);
      std::fprintf(stderr,
                   "[NDPI] Skipping map page %u: undecodable parameters "
                   "(samples_per_pixel=%u bits_per_sample=%u storage=%s "
                   "compression=%u)\n",
                   static_cast<unsigned>(*map_page),
                   static_cast<unsigned>(p.samples_per_pixel),
                   static_cast<unsigned>(p.bits_per_sample),
                   StorageName(p.storage),
                   static_cast<unsigned>(p.compression));
    }
  }

  return aifocore::Status::OkStatus();
}

void NdpiTiffReader::PopulateSlideProperties() {
  properties_.mpp = {0.0, 0.0};
  properties_.objective_magnification = 0.0;

  if (!tiff_index_ || pyramid_levels_.empty()) {
    return;
  }
  const uint16_t page0 = pyramid_levels_[0].page;
  if (page0 >= tiff_index_->NumPages()) {
    return;
  }
  const auto& page = tiff_index_->Page(page0);

  properties_.bounds =
      SlideBounds(0, 0, pyramid_levels_[0].size[0], pyramid_levels_[0].size[1]);

  if (page.x_resolution.has_value() && page.resolution_unit.has_value()) {
    const double res = *page.x_resolution;
    const uint16_t unit = *page.resolution_unit;
    if (res > 0) {
      if (unit == 2) {  // Inch
        properties_.mpp[0] = 25400.0 / res;
      } else if (unit == 3) {  // cm
        properties_.mpp[0] = 10000.0 / res;
      }
    }
  }

  if (page.y_resolution.has_value() && page.resolution_unit.has_value()) {
    const double res = *page.y_resolution;
    const uint16_t unit = *page.resolution_unit;
    if (res > 0) {
      if (unit == 2) {
        properties_.mpp[1] = 25400.0 / res;
      } else if (unit == 3) {
        properties_.mpp[1] = 10000.0 / res;
      }
    }
  }

  if (page.ndpi_source_lens.has_value() && *page.ndpi_source_lens > 0.0) {
    properties_.objective_magnification = *page.ndpi_source_lens;
  } else {
    // Fallback: find first positive SourceLens in the file.
    for (const auto& p : tiff_index_->Pages()) {
      if (p.ndpi_source_lens.has_value() && *p.ndpi_source_lens > 0.0) {
        properties_.objective_magnification = *p.ndpi_source_lens;
        break;
      }
    }
  }
}

aifocore::Status NdpiTiffReader::LoadJpegHeaderTemplate() {
  if (!tiff_index_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "TIFF index not initialized");
  }
  if (tiff_index_->Fd() < 0 || tiff_index_->FileSize() == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Invalid TIFF file descriptor or size");
  }

  // Read a limited prefix; NDPI template header is typically near the start.
  constexpr size_t kSearchLimit = 256 * 1024;
  const size_t read_len = std::min<size_t>(
      static_cast<size_t>(tiff_index_->FileSize()), kSearchLimit);

  std::vector<uint8_t> prefix;
  if (!simpletiff::ReadBytes(tiff_index_->Fd(),
                             static_cast<size_t>(tiff_index_->FileSize()), 0,
                             read_len, prefix)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "Failed to read NDPI file prefix for JPEG header scan");
  }

  AIFOCORE_ASSIGN_OR_RETURN(jpeg_header_template_,
                            FindNdpiJpegHeaderTemplate(prefix));
  AIFOCORE_ASSIGN_OR_RETURN(
      auto offsets,
      FindSofDimensionOffsets(std::span<const uint8_t>(jpeg_header_template_)));
  sof_height_offsets_ = std::move(offsets.first);
  sof_width_offsets_ = std::move(offsets.second);

  return aifocore::Status::OkStatus();
}

aifocore::Status NdpiTiffReader::BuildPatchedJpegHeader(
    uint16_t tile_width, uint16_t tile_height,
    std::vector<uint8_t>& out) const {
  return BuildPatchedNdpiJpegHeader(jpeg_header_template_, sof_height_offsets_,
                                    sof_width_offsets_, tile_width, tile_height,
                                    out);
}

}  // namespace fastslide
