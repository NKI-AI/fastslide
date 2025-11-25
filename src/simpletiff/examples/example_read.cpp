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

// Example: Read a TIFF file and extract a region from the first page

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "simpletiff/index.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_constants.h"
#include "simpletiff/tiff_parser.h"

#ifdef SIMPLETIFF_HAS_PNG
#include "simpletiff/image_writer.h"
#endif

namespace {

bool EndsWith(const char* str, const char* suffix) {
  size_t str_len = std::strlen(str);
  size_t suffix_len = std::strlen(suffix);
  if (suffix_len > str_len)
    return false;
  return std::strcmp(str + str_len - suffix_len, suffix) == 0;
}

void PrintUsage(const char* prog_name) {
  std::cerr << "Usage: " << prog_name << " <tiff_file> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr
      << "  --dump <page_num> <output.png|ppm>  Dump a specific page to file\n";
  std::cerr << "  --info                              Show file info only "
               "(default with no options)\n";
  std::cerr << "\n";
#ifdef SIMPLETIFF_HAS_PNG
  std::cerr << "Supported output formats: PNG, PPM\n";
#else
  std::cerr << "Supported output formats: PPM (PNG support not compiled)\n";
#endif
  std::cerr << "\nExamples:\n";
  std::cerr << "  " << prog_name << " slide.tiff --info\n";
  std::cerr << "  " << prog_name << " slide.tiff --dump 0 page0.png\n";
  std::cerr << "  " << prog_name << " slide.tiff --dump 2 thumbnail.ppm\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const char* input_file = argv[1];

  // Parse command-line arguments
  bool dump_mode = false;
  int dump_page = -1;
  const char* output_file = nullptr;

  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dump") == 0) {
      if (i + 2 >= argc) {
        std::cerr << "Error: --dump requires <page_num> and <output_file>\n";
        PrintUsage(argv[0]);
        return 1;
      }
      dump_mode = true;
      dump_page = std::atoi(argv[i + 1]);
      output_file = argv[i + 2];
      i += 2;
    } else if (std::strcmp(argv[i], "--info") == 0) {
      // Info mode (default)
      dump_mode = false;
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown option: " << argv[i] << "\n";
      PrintUsage(argv[0]);
      return 1;
    }
  }

  // Parse the TIFF file
  simpletiff::TiffIndex index;
  int fd = -1;

  std::cout << "Opening and parsing: " << input_file << "\n";
  if (!simpletiff::OpenTiff(input_file, index, fd)) {
    std::cerr << "Failed to open or parse TIFF file\n";
    return 1;
  }

  std::cout << "Successfully parsed TIFF file\n";
  std::cout << "  BigTIFF: " << (index.IsBigTiff() ? "yes" : "no") << "\n";
  std::cout << "  Little Endian: " << (index.IsLittleEndian() ? "yes" : "no")
            << "\n";
  std::cout << "  File size: " << index.FileSize() << " bytes\n";
  std::cout << "  Number of pages: " << index.NumPages() << "\n\n";

  // Display information about all pages
  for (size_t i = 0; i < index.NumPages(); ++i) {
    const auto& page = index.Page(i);
    std::cout << "=== Page " << i << " ===\n";
    std::cout << "  Image Width: " << page.width
              << " Image Length: " << page.height << "\n";

    // Print description first if available (like tiffinfo)
    if (!page.description.empty()) {
      std::string desc = page.description;
      if (desc.length() > 40) {
        desc = desc.substr(0, 37) + "...";
      }
      std::cout << "  ImageDescription: " << desc << "\n";
    }

    std::cout << "  Samples/Pixel: " << page.samples_per_pixel << "\n";

    // Print resolution if available
    if (page.x_resolution.has_value() || page.y_resolution.has_value()) {
      std::cout << "  Resolution: ";
      if (page.x_resolution.has_value()) {
        std::cout << page.x_resolution.value();
      } else {
        std::cout << "N/A";
      }
      std::cout << ", ";
      if (page.y_resolution.has_value()) {
        std::cout << page.y_resolution.value();
      } else {
        std::cout << "N/A";
      }

      // Print resolution unit if available
      if (page.resolution_unit.has_value()) {
        std::cout << " ";
        uint16_t unit = page.resolution_unit.value();
        switch (unit) {
          case 1:
            std::cout << "(unitless)";
            break;
          case 2:
            std::cout << "pixels/inch";
            break;
          case 3:
            std::cout << "pixels/cm";
            break;
          default:
            std::cout << "(unit=" << unit << ")";
            break;
        }
      }
      std::cout << "\n";
    }

    std::cout << "  Photometric Interpretation: ";
    if (simpletiff::IsPhotometric(page.photometric,
                                  simpletiff::Photometric::kMinIsWhite)) {
      std::cout << "min-is-white";
    } else if (simpletiff::IsPhotometric(
                   page.photometric, simpletiff::Photometric::kMinIsBlack)) {
      std::cout << "min-is-black";
    } else if (simpletiff::IsPhotometric(page.photometric,
                                         simpletiff::Photometric::kRgb)) {
      std::cout << "RGB color";
    } else if (simpletiff::IsPhotometric(page.photometric,
                                         simpletiff::Photometric::kYCbCr)) {
      std::cout << "YCbCr";
    } else {
      std::cout << page.photometric;
    }
    std::cout << "\n";

    std::cout << "  Compression Scheme: ";
    if (simpletiff::IsCompression(page.compression,
                                  simpletiff::Compression::kNone)) {
      std::cout << "None";
    } else if (simpletiff::IsCompression(page.compression,
                                         simpletiff::Compression::kLzw)) {
      std::cout << "LZW";
    } else if (simpletiff::IsCompression(page.compression,
                                         simpletiff::Compression::kJpeg)) {
      std::cout << "JPEG";
    } else if (simpletiff::IsCompression(page.compression,
                                         simpletiff::Compression::kZstd)) {
      std::cout << "ZSTD";
    } else {
      std::cout << page.compression;
    }
    std::cout << "\n";

    // Print predictor if present (like tiffinfo)
    if (page.predictor == 2) {
      std::cout << "  Predictor: horizontal differencing " << page.predictor
                << " (0x" << std::hex << page.predictor << std::dec << ")\n";
    }

    std::cout << "  Storage: ";
    switch (page.storage) {
      case simpletiff::Storage::kTiles: {
        const auto& tiles = index.Tiles(page.payload_id);
        std::cout << "Tiled\n";
        std::cout << "    Tile Width: " << tiles.tile_w
                  << " Tile Length: " << tiles.tile_h << "\n";
        std::cout << "    Tiles: " << tiles.tiles_x << "x" << tiles.tiles_y
                  << " (" << tiles.tiles_x * tiles.tiles_y << " total)\n";
        if (tiles.jpeg_tables_len > 0) {
          std::cout << "    JPEG Tables: (" << tiles.jpeg_tables_len
                    << " bytes)\n";
        }
      } break;
      case simpletiff::Storage::kStrips: {
        const auto& strips = index.Strips(page.payload_id);
        std::cout << "Strips\n";
        std::cout << "    Rows/Strip: " << strips.rows_per_strip << "\n";
        auto offs = index.Offsets(strips.offsets);
        std::cout << "    Strip Count: " << offs.size() << "\n";
        if (strips.jpeg_tables_len > 0) {
          std::cout << "    JPEG Tables: (" << strips.jpeg_tables_len
                    << " bytes)\n";
        }
      } break;
      case simpletiff::Storage::kSingleJpeg:
        std::cout << "Single JPEG\n";
        break;
      case simpletiff::Storage::kUnknown:
        std::cout << "Unknown\n";
        break;
    }
    std::cout << "\n";
  }

  // If no dump mode, just show info and exit
  if (!dump_mode) {
    ::close(fd);
    return 0;
  }

  // Validate page number
  if (dump_page < 0 || static_cast<size_t>(dump_page) >= index.NumPages()) {
    std::cerr << "Error: Page " << dump_page
              << " does not exist (valid range: 0-" << index.NumPages() - 1
              << ")\n";
    ::close(fd);
    return 1;
  }

  const auto& page_to_dump = index.Page(dump_page);

  // Determine region to read
  uint32_t read_width = page_to_dump.width;
  uint32_t read_height = page_to_dump.height;

  // Limit very large pages to avoid excessive memory use
  const uint32_t kMaxDimension = 4096;
  if (read_width > kMaxDimension || read_height > kMaxDimension) {
    std::cout << "Note: Page is large (" << read_width << "x" << read_height
              << "), limiting to " << kMaxDimension << "x" << kMaxDimension
              << " region\n";
    read_width = std::min(read_width, kMaxDimension);
    read_height = std::min(read_height, kMaxDimension);
  }

  const int channels = page_to_dump.samples_per_pixel;
  std::cout << "Dumping page " << dump_page << ": " << read_width << "x"
            << read_height << " pixels (" << channels << " channel"
            << (channels == 1 ? "" : "s") << ")\n";

  simpletiff::Roi roi{0, 0, read_width, read_height};
  const int stride = static_cast<int>(read_width) * channels;
  std::vector<uint8_t> buffer(static_cast<size_t>(read_width) * read_height *
                              channels);

  simpletiff::DecodeContext ctx;
  auto result =
      simpletiff::ReadPage(index, dump_page, roi, ctx, buffer.data(), stride);
  if (!result) {
    std::cerr << "Failed to read page " << dump_page << ": "
              << result.error().message << "\n";
    ::close(fd);
    return 1;
  }

  std::cout << "Successfully read page data\n";

  // Write output
  std::cout << "Writing output to: " << output_file << "\n";

  bool success = false;
  const int out_width = static_cast<int>(read_width);
  const int out_height = static_cast<int>(read_height);

#ifdef SIMPLETIFF_HAS_PNG
  if (EndsWith(output_file, ".png") || EndsWith(output_file, ".PNG")) {
    if (channels == 1) {
      success = simpletiff::WritePngGrayscale(output_file, buffer.data(),
                                              out_width, out_height, stride);
    } else {
      success = simpletiff::WritePng(output_file, buffer.data(), out_width,
                                     out_height, stride);
    }
  } else
#endif
      if (EndsWith(output_file, ".ppm") || EndsWith(output_file, ".PPM")) {
    if (channels != 3) {
      std::cerr << "PPM format only supports 3-channel (RGB) images\n";
      ::close(fd);
      return 1;
    }
    success = simpletiff::WritePpm(output_file, buffer.data(), out_width,
                                   out_height, stride);
  } else {
    std::cerr << "Unknown output format (use .png or .ppm extension)\n";
    ::close(fd);
    return 1;
  }

  if (!success) {
    std::cerr << "Failed to write output file\n";
    ::close(fd);
    return 1;
  }

  std::cout << "Output written successfully\n";

  ::close(fd);
  return 0;
}
