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

#include <fastslide/fastslide.h>
#include <fastslide/runtime/reader_registry.h>
#include <CLI11/CLI11.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  CLI::App app{"FastSlide Read Region Example"};

  std::string path;
  app.add_option("path", path, "Path to slide image")->required();

  uint32_t x_coord = 0;
  uint32_t y_coord = 0;
  app.add_option("-x", x_coord, "X coordinate of region")->default_val(0);
  app.add_option("-y", y_coord, "Y coordinate of region")->default_val(0);

  uint32_t width = 256;
  uint32_t height = 256;
  app.add_option("-W,--width", width, "Width of region")->default_val(256);
  app.add_option("-H,--height", height, "Height of region")->default_val(256);

  int level = 0;
  app.add_option("-l,--level", level, "Pyramid level")->default_val(0);

  CLI11_PARSE(app, argc, argv);

  const fastslide::ImageDimensions size = {width, height};
  const fastslide::ImageCoordinate location = {x_coord, y_coord};

  auto reader_or = fastslide::runtime::GetGlobalRegistry().CreateReader(path);
  if (!reader_or.ok()) {
    std::cerr << reader_or.status() << "\n";
    return 1;
  }

  auto reader = std::move(*reader_or);

  const int level_count = reader->GetLevelCount();
  auto level0_info = reader->GetLevelInfo(0);
  if (!level0_info.ok()) {
    std::cerr << level0_info.status() << "\n";
    return 1;
  }
  const auto dims = level0_info->dimensions;
  const auto& props = reader->GetProperties();
  const auto& bounds = props.bounds;

  std::cout << "Dimensions: " << dims[0] << " x " << dims[1] << "\n";
  std::cout << "Levels: " << level_count << "\n";
  std::cout << "Resolution (mpp): " << props.mpp[0] << ", " << props.mpp[1]
            << " microns/pixel\n";
  std::cout << "Format: " << reader->GetFormatName() << "\n";
  std::cout << "Bounds (tissue region):\n";
  std::cout << "  x: " << bounds.x << "\n";
  std::cout << "  y: " << bounds.y << "\n";
  std::cout << "  width: " << bounds.width << "\n";
  std::cout << "  height: " << bounds.height << "\n";
  std::cout << "Pixel location (level 0, with bounds): (" << location[0] << ", "
            << location[1] << ")\n";

  fastslide::RegionSpec region{
      .top_left = location, .size = size, .level = level};

  // Check if region is completely out of bounds
  if (location[0] >= dims[0] || location[1] >= dims[1]) {
    std::cerr << "Error: Requested region is completely out of bounds.\n";
    return 1;
  }

  // Clamp region to image bounds
  // This is optional if using SlideReader::ClampRegion, but good practice
  auto clamped_region = fastslide::SlideReader::ClampRegion(region, dims);
  std::cout << "Clamped region size: " << clamped_region.size[0] << " x "
            << clamped_region.size[1] << "\n";

  auto image_or = reader->ReadRegion(region);
  if (!image_or.ok()) {
    std::cerr << image_or.status() << "\n";
    return 1;
  }

  const auto& image = *image_or;
  std::cout << "Read region info:\n";
  std::cout << "  Channels: " << image.GetChannels() << "\n";
  std::cout << "  Data Type: " << fastslide::GetName(image.GetDataType())
            << "\n";
  std::cout << "  Dimensions: " << image.GetWidth() << " x "
            << image.GetHeight() << "\n";
}