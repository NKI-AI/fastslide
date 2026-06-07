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

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include "CLI11/CLI11.hpp"
#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/fastslide.h"
#include "fastslide/readers/mrxs/mrxs.h"
#include "fastslide/runtime/decoders/png_decoder.h"
#include "fastslide/slide_image.h"

namespace {

// Forward declaration (used by DumpAssociatedImages, defined later).
aifocore::Status SaveImagePNG(const fastslide::Image& image,
                              const std::string& filename);

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

void PrintSlideInfo(const fastslide::SlideReader& reader,
                    const fastslide::SlideImage& image) {
  PrintHeader("Slide Information");

  // Reader-level
  PrintKeyValue("Format", reader.GetFormatName());
  PrintKeyValue("Image Format", fastslide::GetName(image.GetImageFormat()));

  // Per-image properties
  const auto& props = image.GetProperties();
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

  // Tile size (per-image)
  auto tile_size = image.GetTileSize();
  std::string tile_str =
      std::to_string(tile_size[0]) + " x " + std::to_string(tile_size[1]);
  PrintKeyValue("Tile Size", tile_str);
}

void PrintImages(const fastslide::SlideReader& reader) {
  const int count = reader.GetImageCount();
  // Always show this section so users know the file is single- or
  // multi-image.
  PrintHeader("Images");
  PrintKeyValue("Number of Images", count);
  PrintKeyValue("Primary Image Index", reader.GetPrimaryImageIndex());

  const auto names = reader.GetImageNames();
  for (int i = 0; i < count; ++i) {
    auto image_or = reader.GetImage(i);
    if (!image_or.ok()) {
      PrintSubHeader("Image " + std::to_string(i));
      PrintKeyValue("  Error", image_or.status().ToString(), 25);
      continue;
    }
    const auto* image = image_or.value();
    const std::string name =
        i < static_cast<int>(names.size()) ? names[i] : std::string{};

    PrintSubHeader("Image " + std::to_string(i) +
                   (name.empty() ? "" : " (" + name + ")"));

    auto level0 = image->GetLevelInfo(0);
    if (level0.ok()) {
      std::string dims = std::to_string(level0->dimensions[0]) + " x " +
                         std::to_string(level0->dimensions[1]);
      PrintKeyValue("  Level 0 Dimensions", dims, 25);
    }
    PrintKeyValue("  Levels", image->GetLevelCount(), 25);
    auto tile = image->GetTileSize();
    PrintKeyValue("  Tile Size",
                  std::to_string(tile[0]) + " x " + std::to_string(tile[1]),
                  25);
    // Surface the focal (Z) / time (T) stack extent when the image has one.
    const auto stack = image->GetStackInfo();
    if (stack.z_count > 1 || stack.t_count > 1) {
      PrintKeyValue("  Focal Planes (Z)", static_cast<size_t>(stack.z_count),
                    25);
      PrintKeyValue("  Time Points (T)", static_cast<size_t>(stack.t_count),
                    25);
    }
    const auto& props = image->GetProperties();
    PrintKeyValue("  MPP X", props.mpp[0], 25);
    PrintKeyValue("  MPP Y", props.mpp[1], 25);
  }
}

void PrintLevelInfo(const fastslide::SlideImage& image) {
  PrintHeader("Pyramid Levels");

  int level_count = image.GetLevelCount();
  PrintKeyValue("Number of Levels", level_count);

  for (int level = 0; level < level_count; ++level) {
    auto level_info_or = image.GetLevelInfo(level);
    if (!level_info_or.ok()) {
      std::cerr << "Error reading level " << level << ": "
                << level_info_or.status().ToString() << '\n';
      continue;
    }

    const auto& info = *level_info_or;
    PrintSubHeader("Level " + std::to_string(level));

    std::string dims_str = std::to_string(info.dimensions[0]) + " x " +
                           std::to_string(info.dimensions[1]);
    PrintKeyValue("  Dimensions", dims_str, 25);
    PrintKeyValue("  Downsample Factor", info.downsample_factor, 25);

    const auto& props = image.GetProperties();
    double level_mpp_x = props.mpp[0] * info.downsample_factor;
    double level_mpp_y = props.mpp[1] * info.downsample_factor;

    std::string mpp_str =
        std::to_string(level_mpp_x) + " x " + std::to_string(level_mpp_y);
    PrintKeyValue("  Approx MPP", mpp_str, 25);

    double width_mm = info.dimensions[0] * level_mpp_x / 1000.0;
    double height_mm = info.dimensions[1] * level_mpp_y / 1000.0;
    std::string size_mm =
        std::to_string(width_mm) + " x " + std::to_string(height_mm) + " mm";
    PrintKeyValue("  Physical Size", size_mm, 25);
  }
}

void PrintChannelInfo(const fastslide::SlideImage& image) {
  auto channels = image.GetChannelMetadata();
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

void DumpAssociatedImages(const fastslide::SlideReader& reader,
                          const std::string& out_dir) {
  auto assoc_names = reader.GetAssociatedImageNames();
  if (assoc_names.empty()) {
    return;
  }

  PrintHeader("Dumping Associated Images");
  PrintKeyValue("Output Directory", out_dir);

  for (const auto& name : assoc_names) {
    auto img_or = reader.ReadAssociatedImage(name);
    if (!img_or.ok()) {
      std::cerr << "Failed to read associated image '" << name
                << "': " << img_or.status().ToString() << "\n";
      continue;
    }

    const auto& img = *img_or;
    std::string filename = out_dir + "/" + std::string(name) + ".png";
    auto st = SaveImagePNG(img, filename);
    if (!st.ok()) {
      std::cerr << "Failed to write '" << filename << "': " << st.ToString()
                << "\n";
      continue;
    }
    std::cout << "Wrote: " << filename << "\n";
  }
}

void PrintAssociatedData(const fastslide::SlideReader& reader) {
  // Check if this is an MRXS reader (only format that supports associated data
  // currently)
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

// Save a separate-planar (spectral) image as one grayscale PNG per
// channel. Used for multi-channel 16-bit fluorescence, where a single
// interleaved PNG would not represent the independent fluorophore
// planes. Output files are suffixed ``_cN`` before the extension.
aifocore::Status SaveSpectralImagePNGs(const fastslide::Image& image,
                                       const std::string& filename) {
  const uint32_t channels = image.GetChannels();
  const auto dtype = image.GetDataType();
  const uint32_t bit_depth = dtype == fastslide::DataType::kUInt16 ? 16U : 8U;
  const std::size_t bytes_per_sample = bit_depth / 8U;
  const std::size_t plane_samples =
      static_cast<std::size_t>(image.GetWidth()) * image.GetHeight();
  const std::size_t plane_bytes = plane_samples * bytes_per_sample;

  const auto dot = filename.find_last_of('.');
  const std::string stem =
      dot == std::string::npos ? filename : filename.substr(0, dot);
  const std::string ext =
      dot == std::string::npos ? ".png" : filename.substr(dot);

  for (uint32_t c = 0; c < channels; ++c) {
    const std::string out = stem + "_c" + std::to_string(c) + ext;
    const std::span<const uint8_t> plane(image.GetData() + c * plane_bytes,
                                         plane_bytes);
    AIFOCORE_RETURN_IF_ERROR(fastslide::runtime::decoders::EncodePngToFile(
        out, plane, image.GetWidth(), image.GetHeight(), /*channels=*/1U,
        bit_depth));
    std::cout << "  wrote channel " << c << " -> " << out << '\n';
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status SaveImagePNG(const fastslide::Image& image,
                              const std::string& filename) {
  // Separate-planar images (multi-channel fluorescence) are written as
  // one grayscale PNG per channel.
  if (image.GetPlanarConfig() == fastslide::PlanarConfig::kSeparate &&
      image.GetChannels() > 1U) {
    return SaveSpectralImagePNGs(image, filename);
  }

  if (image.GetPlanarConfig() != fastslide::PlanarConfig::kContiguous) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Only contiguous images can be saved");
  }

  const auto format = image.GetFormat();
  if (format != fastslide::ImageFormat::kRGB &&
      format != fastslide::ImageFormat::kRGBA &&
      format != fastslide::ImageFormat::kGray) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Only Gray/RGB/RGBA images can be saved");
  }

  const auto dtype = image.GetDataType();
  if (dtype != fastslide::DataType::kUInt8 &&
      dtype != fastslide::DataType::kUInt16) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Only uint8 and uint16 images can be saved");
  }

  const uint32_t channels = image.GetChannels();
  const uint32_t bit_depth = dtype == fastslide::DataType::kUInt16 ? 16U : 8U;
  const std::size_t bytes_per_sample = bit_depth / 8U;
  const std::size_t pixel_bytes = static_cast<std::size_t>(channels) *
                                  image.GetWidth() * image.GetHeight() *
                                  bytes_per_sample;
  return fastslide::runtime::decoders::EncodePngToFile(
      filename, std::span<const uint8_t>(image.GetData(), pixel_bytes),
      image.GetWidth(), image.GetHeight(), channels, bit_depth);
}

int InfoCommand(const std::string& input_file, bool verbose, int image_index) {
  std::cout << "Opening slide: " << input_file << '\n';
  auto reader_or = fastslide::GetGlobalRegistry().CreateReader(input_file);

  if (!reader_or.ok()) {
    std::cerr << "\nError: Failed to open slide\n";
    std::cerr << "Status: " << reader_or.status().ToString() << '\n';
    return 1;
  }

  auto& reader = *reader_or;

  // Negative index means "use the primary image".
  const int resolved_index =
      image_index < 0 ? reader->GetPrimaryImageIndex() : image_index;
  auto image_or = reader->GetImage(resolved_index);
  if (!image_or.ok()) {
    std::cerr << "Error: invalid --image index " << resolved_index << ": "
              << image_or.status().ToString() << '\n';
    return 1;
  }
  const auto& image = *image_or.value();

  PrintSlideInfo(*reader, image);
  PrintImages(*reader);
  PrintLevelInfo(image);
  PrintChannelInfo(image);
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
                  const std::string& output_file, int image_index) {
  std::cout << "Opening slide: " << input_file << '\n';
  auto reader_or = fastslide::GetGlobalRegistry().CreateReader(input_file);

  if (!reader_or.ok()) {
    std::cerr << "Error: Failed to open slide\n";
    std::cerr << "Status: " << reader_or.status().ToString() << '\n';
    return 1;
  }

  auto& reader = *reader_or;

  const int resolved_index =
      image_index < 0 ? reader->GetPrimaryImageIndex() : image_index;

  std::cout << "Reading region:\n";
  std::cout << "  Position: (" << std::fixed << std::setprecision(2) << x
            << ", " << y << ") [FRACTIONAL]\n";
  std::cout << "  Size: " << width << " x " << height << " pixels\n";
  std::cout << "  Level: " << level << '\n';
  std::cout << "  Format: " << reader->GetFormatName() << '\n';
  std::cout << "  Image Index: " << resolved_index;
  if (image_index < 0) {
    std::cout << " (primary)";
  }
  std::cout << '\n';

  aifocore::Result<fastslide::Image> image_or =
      aifocore::Status(aifocore::StatusCode::kUnknown, "Uninitialized");

  // MRXS keeps its fractional fast path, and is single-image, so the
  // --image flag is only meaningful for multi-image formats like VSI.
  if (reader->GetFormatName() == "MRXS") {
    if (image_index > 0) {
      std::cerr << "Error: MRXS only exposes a single image (got --image "
                << image_index << ")\n";
      return 1;
    }
    std::cout << "  Using MRXS fractional coordinate path...\n";
    auto* mrxs_reader = dynamic_cast<fastslide::MrxsReader*>(reader.get());
    if (mrxs_reader) {
      auto result =
          mrxs_reader->ReadRegionFractional(level, x, y, width, height);
      if (result.ok()) {
        image_or = std::move(*result);
      } else {
        image_or = result.status();
      }
    } else {
      std::cerr << "Error: Failed to cast to MrxsReader\n";
      return 1;
    }
  } else {
    auto target_or = reader->GetImage(resolved_index);
    if (!target_or.ok()) {
      std::cerr << "Error: invalid --image index " << resolved_index << ": "
                << target_or.status().ToString() << '\n';
      return 1;
    }
    fastslide::RegionSpec region{
        .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
        .size = {width, height},
        .level = level};
    image_or = target_or.value()->ReadRegion(region);
  }

  if (!image_or.ok()) {
    std::cerr << "Error: Failed to read region\n";
    std::cerr << "Status: " << image_or.status().ToString() << '\n';
    return 1;
  }

  const auto& image = *image_or;
  std::cout << "Read image: " << image.GetWidth() << " x " << image.GetHeight()
            << " pixels\n";
  std::cout << "  Channels: " << image.GetChannels() << '\n';

  // Save image
  std::cout << "Saving to: " << output_file << '\n';
  auto save_status = SaveImagePNG(image, output_file);
  if (!save_status.ok()) {
    std::cerr << "Error: Failed to save image\n";
    std::cerr << "Status: " << save_status.ToString() << '\n';
    return 1;
  }

  std::cout << "Successfully saved region!\n";
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  CLI::App app{"FastSlide Tool - Slide Reader Utility"};

  // Common flags
  std::string input_file;
  bool verbose = false;
  bool dump_associated_images = false;
  std::string associated_images_dir = ".";
  // Default -1 means "use the primary image". Files like Olympus VSI can
  // expose several pyramids; this flag picks which one to operate on.
  int image_index = -1;

  // Region flags
  double x = 0.0;
  double y = 0.0;
  uint32_t width = 512;
  uint32_t height = 512;
  int level = 0;
  std::string output_file = "output.png";

  app.require_subcommand(1);

  // Info command
  auto* info_cmd = app.add_subcommand("info", "Show slide information");
  info_cmd
      ->add_option("--input,-i", input_file,
                   "Path to the slide file (SVS, QPTIFF, or MRXS)")
      ->required();
  info_cmd->add_flag("--verbose,-v", verbose,
                     "Show verbose information including metadata");
  info_cmd->add_flag(
      "--dump-associated-images", dump_associated_images,
      "Write associated images (e.g. label, macro) to PNG files");
  info_cmd->add_option(
      "--associated-images-dir", associated_images_dir,
      "Output directory for --dump-associated-images (default: current dir)");
  info_cmd->add_option(
      "--image", image_index,
      "Image index to inspect (default: primary). Use the 'Images' section "
      "to list available indices for multi-image formats like Olympus VSI.");

  // Region command
  auto* region_cmd =
      app.add_subcommand("region", "Read and save a region from the slide");
  region_cmd
      ->add_option("--input,-i", input_file,
                   "Path to the slide file (SVS, QPTIFF, or MRXS)")
      ->required();
  region_cmd->add_option("--x,-x", x, "X coordinate (top-left)")
      ->default_val(0.0);
  region_cmd->add_option("--y,-y", y, "Y coordinate (top-left)")
      ->default_val(0.0);
  region_cmd->add_option("--width,-W", width, "Region width in pixels")
      ->default_val(512);
  region_cmd->add_option("--height,-H", height, "Region height in pixels")
      ->default_val(512);
  region_cmd->add_option("--level,-l", level, "Pyramid level to read from")
      ->default_val(0);
  region_cmd
      ->add_option("--output,-o", output_file, "Output file path (PNG or JPEG)")
      ->default_val("output.png");
  region_cmd->add_option(
      "--image", image_index,
      "Image index to read from (default: primary). Use 'info' to list "
      "available indices for multi-image formats like Olympus VSI.");

  CLI11_PARSE(app, argc, argv);

  if (info_cmd->parsed()) {
    int rc = InfoCommand(input_file, verbose, image_index);
    if (rc != 0) {
      return rc;
    }

    if (dump_associated_images) {
      auto reader_or = fastslide::GetGlobalRegistry().CreateReader(input_file);
      if (!reader_or.ok()) {
        std::cerr << "\nError: Failed to open slide for dumping images\n";
        std::cerr << "Status: " << reader_or.status().ToString() << '\n';
        return 1;
      }
      DumpAssociatedImages(*(*reader_or), associated_images_dir);
    }
    return 0;
  }

  if (region_cmd->parsed()) {
    return RegionCommand(input_file, x, y, width, height, level, output_file,
                         image_index);
  }

  return 0;
}
