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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "fastslide/fastslide.h"
#include "fastslide/readers/mrxs/mrxs.h"
#include "lodepng/lodepng.h"

// Common flags
ABSL_FLAG(std::string, input, "",
          "Path to the slide file (SVS, QPTIFF, or MRXS)");

// Info command flags
ABSL_FLAG(bool, verbose, false, "Show verbose information including metadata");

// Region command flags
ABSL_FLAG(double, x, 0.0, "X coordinate (top-left)");
ABSL_FLAG(double, y, 0.0, "Y coordinate (top-left)");
ABSL_FLAG(uint32_t, width, 512, "Region width in pixels");
ABSL_FLAG(uint32_t, height, 512, "Region height in pixels");
ABSL_FLAG(int, level, 0, "Pyramid level to read from");
ABSL_FLAG(std::string, output, "output.png", "Output file path (PNG or JPEG)");

namespace {

void PrintSeparator(char c = '=') {
  std::cout << std::string(80, c) << '\n';
}

void PrintHeader(const std::string& title) {
  std::cout << '\n';
  PrintSeparator('=');
  std::cout << " " << title << '\n';
  PrintSeparator('=');
}

void PrintSubHeader(const std::string& title) {
  std::cout << '\n';
  std::cout << "--- " << title << " ---\n";
}

void PrintKeyValue(const std::string& key, const std::string& value,
                   int width = 30) {
  std::cout << std::left << std::setw(width) << (key + ":") << value << '\n';
}

void PrintKeyValue(const std::string& key, double value, int width = 30) {
  std::cout << std::left << std::setw(width) << (key + ":") << std::fixed
            << std::setprecision(6) << value << '\n';
}

void PrintKeyValue(const std::string& key, int value, int width = 30) {
  std::cout << std::left << std::setw(width) << (key + ":") << value << '\n';
}

void PrintKeyValue(const std::string& key, size_t value, int width = 30) {
  std::cout << std::left << std::setw(width) << (key + ":") << value << '\n';
}

void PrintSlideInfo(const fastslide::SlideReader& reader) {
  PrintHeader("Slide Information");

  // Format
  PrintKeyValue("Format", reader.GetFormatName());
  PrintKeyValue("Image Format", fastslide::GetName(reader.GetImageFormat()));

  // Properties
  const auto& props = reader.GetProperties();
  PrintKeyValue("MPP X", props.mpp[0]);
  PrintKeyValue("MPP Y", props.mpp[1]);
  PrintKeyValue("Objective Magnification", props.objective_magnification);

  if (!props.objective_name.empty()) {
    PrintKeyValue("Objective Name", props.objective_name);
  }
  if (!props.scanner_model.empty()) {
    PrintKeyValue("Scanner Model", props.scanner_model);
  }
  if (props.scan_date.has_value()) {
    PrintKeyValue("Scan Date", *props.scan_date);
  }

  // Tile size
  auto tile_size = reader.GetTileSize();
  std::string tile_str =
      std::to_string(tile_size[0]) + " x " + std::to_string(tile_size[1]);
  PrintKeyValue("Tile Size", tile_str);

  // MRXS-specific: Camera position information
  if (reader.GetFormatName() == "MRXS") {
    const auto* mrxs_reader =
        dynamic_cast<const fastslide::MrxsReader*>(&reader);
    if (mrxs_reader) {
      const auto& mrxs_info = mrxs_reader->GetMrxsInfo();
      if (!mrxs_info.using_synthetic_positions) {
        const int positions_x = mrxs_info.images_x / mrxs_info.image_divisions;
        const int positions_y = mrxs_info.images_y / mrxs_info.image_divisions;
        std::string pos_str =
            std::to_string(mrxs_info.camera_positions.size() / 2) + " (" +
            std::to_string(positions_x) + " x " + std::to_string(positions_y) +
            ") [FROM FILE]";
        PrintKeyValue("Camera Positions", pos_str);
      } else {
        PrintKeyValue("Camera Positions", "SYNTHETIC (uniform grid)");
      }
    }
  }
}

void PrintLevelInfo(const fastslide::SlideReader& reader) {
  PrintHeader("Pyramid Levels");

  int level_count = reader.GetLevelCount();
  PrintKeyValue("Number of Levels", level_count);

  for (int level = 0; level < level_count; ++level) {
    auto level_info_or = reader.GetLevelInfo(level);
    if (!level_info_or.ok()) {
      std::cerr << "Error reading level " << level << ": "
                << level_info_or.status() << '\n';
      continue;
    }

    const auto& info = *level_info_or;
    PrintSubHeader("Level " + std::to_string(level));

    std::string dims_str = std::to_string(info.dimensions[0]) + " x " +
                           std::to_string(info.dimensions[1]);
    PrintKeyValue("  Dimensions", dims_str, 25);
    PrintKeyValue("  Downsample Factor", info.downsample_factor, 25);

    // Calculate approximate MPP for this level
    const auto& props = reader.GetProperties();
    double level_mpp_x = props.mpp[0] * info.downsample_factor;
    double level_mpp_y = props.mpp[1] * info.downsample_factor;

    std::string mpp_str =
        std::to_string(level_mpp_x) + " x " + std::to_string(level_mpp_y);
    PrintKeyValue("  Approx MPP", mpp_str, 25);

    // Calculate physical size in mm
    double width_mm = info.dimensions[0] * level_mpp_x / 1000.0;
    double height_mm = info.dimensions[1] * level_mpp_y / 1000.0;
    std::string size_mm =
        std::to_string(width_mm) + " x " + std::to_string(height_mm) + " mm";
    PrintKeyValue("  Physical Size", size_mm, 25);
  }
}

void PrintChannelInfo(const fastslide::SlideReader& reader) {
  auto channels = reader.GetChannelMetadata();
  if (channels.empty()) {
    return;
  }

  PrintHeader("Channel Information");
  PrintKeyValue("Number of Channels", channels.size());

  for (size_t i = 0; i < channels.size(); ++i) {
    const auto& ch = channels[i];
    PrintSubHeader("Channel " + std::to_string(i));

    PrintKeyValue("  Name", ch.name, 25);
    if (!ch.biomarker.empty()) {
      PrintKeyValue("  Biomarker", ch.biomarker, 25);
    }

    std::string color_str = "RGB(" + std::to_string(ch.color.r) + ", " +
                            std::to_string(ch.color.g) + ", " +
                            std::to_string(ch.color.b) + ")";
    PrintKeyValue("  Color", color_str, 25);

    if (ch.exposure_time > 0) {
      PrintKeyValue("  Exposure Time (μs)",
                    static_cast<size_t>(ch.exposure_time), 25);
    }
    if (ch.signal_units > 0) {
      PrintKeyValue("  Signal Units", static_cast<size_t>(ch.signal_units), 25);
    }
  }
}

void PrintAssociatedImages(const fastslide::SlideReader& reader) {
  auto assoc_names = reader.GetAssociatedImageNames();
  if (assoc_names.empty()) {
    return;
  }

  PrintHeader("Associated Images");
  PrintKeyValue("Number of Images", assoc_names.size());

  for (const auto& name : assoc_names) {
    auto dims_or = reader.GetAssociatedImageDimensions(name);
    if (dims_or.ok()) {
      const auto& dims = *dims_or;
      std::string size_str =
          std::to_string(dims[0]) + " x " + std::to_string(dims[1]);
      PrintKeyValue("  " + name, size_str, 25);
    } else {
      PrintKeyValue("  " + name, "unknown size", 25);
    }
  }
}

void PrintAssociatedData(const fastslide::SlideReader& reader) {
  // Check if this is an MRXS reader (only format that supports associated data currently)
  const auto* mrxs_reader = dynamic_cast<const fastslide::MrxsReader*>(&reader);
  if (!mrxs_reader) {
    return;
  }

  auto data_names = mrxs_reader->GetAssociatedDataNames();
  if (data_names.empty()) {
    return;
  }

  PrintHeader("Associated Data (Non-Hierarchical Layers)");
  PrintKeyValue("Number of Data Items", data_names.size());

  for (const auto& name : data_names) {
    auto info_or = mrxs_reader->GetAssociatedDataInfo(name);
    if (info_or.ok()) {
      const auto& info = *info_or;
      std::string type_str = fastslide::GetTypeName(info.type);
      PrintKeyValue("  " + name, type_str, 50);
    } else {
      PrintKeyValue("  " + name, "error", 50);
    }
  }

  std::cout << "\nNote: Use LoadAssociatedData(name) to access the actual data "
               "(lazy-loaded)\n";
}

void PrintMetadata(const fastslide::SlideReader& reader) {
  auto metadata = reader.GetMetadata();
  if (metadata.empty()) {
    return;
  }

  PrintHeader("Additional Metadata");

  for (const auto& [key, value] : metadata) {
    std::string value_str;

    if (std::holds_alternative<std::string>(value)) {
      value_str = std::get<std::string>(value);
    } else if (std::holds_alternative<size_t>(value)) {
      value_str = std::to_string(std::get<size_t>(value));
    } else if (std::holds_alternative<double>(value)) {
      value_str = std::to_string(std::get<double>(value));
    }

    PrintKeyValue("  " + key, value_str, 35);
  }
}

// PNG image writer using lodepng
absl::Status SaveImagePNG(const fastslide::Image& image,
                          const std::string& filename) {
  if (image.GetFormat() != fastslide::ImageFormat::kRGB) {
    return absl::InvalidArgumentError("Only RGB images can be saved");
  }

  if (image.GetDataType() != fastslide::DataType::kUInt8) {
    return absl::InvalidArgumentError("Only uint8 images can be saved");
  }

  // Use lodepng to encode RGB image as PNG
  unsigned int error =
      lodepng_encode24_file(filename.c_str(), image.GetData(),
                            static_cast<unsigned int>(image.GetWidth()),
                            static_cast<unsigned int>(image.GetHeight()));

  if (error != 0) {
    return absl::InternalError(absl::StrFormat("PNG encode error %d: %s", error,
                                               lodepng_error_text(error)));
  }

  return absl::OkStatus();
}

int InfoCommand(const std::string& input_file, bool verbose) {
  // Create reader using global registry
  std::cout << "Opening slide: " << input_file << '\n';
  auto reader_or = fastslide::GetGlobalRegistry().CreateReader(input_file);

  if (!reader_or.ok()) {
    std::cerr << "\nError: Failed to open slide\n";
    std::cerr << "Status: " << reader_or.status() << '\n';
    return 1;
  }

  auto& reader = *reader_or;

  // Print information
  PrintSlideInfo(*reader);
  PrintLevelInfo(*reader);
  PrintChannelInfo(*reader);
  PrintAssociatedImages(*reader);
  PrintAssociatedData(*reader);

  if (verbose) {
    PrintMetadata(*reader);
  }

  std::cout << '\n';
  PrintSeparator('=');
  std::cout << "Successfully read slide information!\n";
  PrintSeparator('=');
  std::cout << '\n';

  return 0;
}

int RegionCommand(const std::string& input_file, double x, double y,
                  uint32_t width, uint32_t height, int level,
                  const std::string& output_file) {
  // Create reader using global registry
  std::cout << "Opening slide: " << input_file << '\n';
  auto reader_or = fastslide::GetGlobalRegistry().CreateReader(input_file);

  if (!reader_or.ok()) {
    std::cerr << "Error: Failed to open slide\n";
    std::cerr << "Status: " << reader_or.status() << '\n';
    return 1;
  }

  auto& reader = *reader_or;

  std::cout << "Reading region:\n";
  std::cout << "  Position: (" << std::fixed << std::setprecision(2) << x
            << ", " << y << ") [FRACTIONAL]\n";
  std::cout << "  Size: " << width << " x " << height << " pixels\n";
  std::cout << "  Level: " << level << '\n';
  std::cout << "  Format: " << reader->GetFormatName() << '\n';

  // For MRXS, use ReadRegionFractional to preserve fractional coordinates
  absl::StatusOr<fastslide::Image> image_or;

  if (reader->GetFormatName() == "MRXS") {
    std::cout << "  Using MRXS fractional coordinate path...\n";
    auto* mrxs_reader = dynamic_cast<fastslide::MrxsReader*>(reader.get());
    if (mrxs_reader) {
      image_or = mrxs_reader->ReadRegionFractional(level, x, y, width, height);
    } else {
      std::cerr << "Error: Failed to cast to MrxsReader\n";
      return 1;
    }
  } else {
    // For other formats, use standard RegionSpec (truncates to uint32)
    fastslide::RegionSpec region{
        .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
        .size = {width, height},
        .level = level};
    image_or = reader->ReadRegion(region);
  }

  if (!image_or.ok()) {
    std::cerr << "Error: Failed to read region\n";
    std::cerr << "Status: " << image_or.status() << '\n';
    return 1;
  }

  const auto& image = *image_or;
  std::cout << "Read image: " << image.GetWidth() << " x " << image.GetHeight()
            << " pixels\n";

  // Save image
  std::cout << "Saving to: " << output_file << '\n';
  auto save_status = SaveImagePNG(image, output_file);
  if (!save_status.ok()) {
    std::cerr << "Error: Failed to save image\n";
    std::cerr << "Status: " << save_status << '\n';
    return 1;
  }

  std::cout << "Successfully saved region!\n";
  return 0;
}

void PrintUsage(const char* program_name) {
  std::cerr << "Usage: " << program_name << " <command> [options]\n\n";
  std::cerr << "Commands:\n";
  std::cerr << "  info     Show slide information (default)\n";
  std::cerr << "  region   Read and save a region from the slide\n";
  std::cerr << "\n";
  std::cerr << "Common options:\n";
  std::cerr << "  --input=<path>       Path to slide file (required)\n";
  std::cerr << "\n";
  std::cerr << "Info command options:\n";
  std::cerr << "  --verbose            Show detailed metadata\n";
  std::cerr << "\n";
  std::cerr << "Region command options:\n";
  std::cerr << "  --x=<value>          X coordinate (default: 0.0)\n";
  std::cerr << "  --y=<value>          Y coordinate (default: 0.0)\n";
  std::cerr << "  --width=<pixels>     Region width (default: 512)\n";
  std::cerr << "  --height=<pixels>    Region height (default: 512)\n";
  std::cerr << "  --level=<n>          Pyramid level (default: 0)\n";
  std::cerr
      << "  --output=<path>      Output file path (default: output.png)\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  " << program_name << " info --input=slide.mrxs\n";
  std::cerr << "  " << program_name << " info --input=slide.svs --verbose\n";
  std::cerr << "  " << program_name
            << " region --input=slide.mrxs --x=1000 --y=2000 --width=1024 "
               "--height=1024\n";
  std::cerr << "  " << program_name
            << " region --input=slide.qptiff --level=2 --output=region.png\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  // Get command
  std::string command = argv[1];

  // Parse remaining flags
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  std::string input_file = absl::GetFlag(FLAGS_input);

  if (input_file.empty()) {
    std::cerr << "Error: --input flag is required\n\n";
    PrintUsage(argv[0]);
    return 1;
  }

  // Execute command
  if (command == "info") {
    bool verbose = absl::GetFlag(FLAGS_verbose);
    return InfoCommand(input_file, verbose);
  } else if (command == "region") {
    double x = absl::GetFlag(FLAGS_x);
    double y = absl::GetFlag(FLAGS_y);
    uint32_t width = absl::GetFlag(FLAGS_width);
    uint32_t height = absl::GetFlag(FLAGS_height);
    int level = absl::GetFlag(FLAGS_level);
    std::string output = absl::GetFlag(FLAGS_output);
    return RegionCommand(input_file, x, y, width, height, level, output);
  } else {
    std::cerr << "Error: Unknown command '" << command << "'\n\n";
    PrintUsage(argv[0]);
    return 1;
  }
}
