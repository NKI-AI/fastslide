// Copyright 2025 Jonas Teuwen & Joren Brunekreef. All Rights Reserved.
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

#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahcore/pathology/config/inference_config.h"
#include "ahcore/pathology/inference_engine_fimage.h"
#include "aifocore/utilities/spinners.h"

#include "CLI11/CLI11.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  spdlog::set_level(spdlog::level::info);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e][%^%l%$] %v");

  // Parse image/mask using CLI11 allowing extras, then parse engine flags
  // with InferenceConfig::FromArgs (which also allows extras).
  CLI::App app{"Ahcore inference with FImage backend"};
  app.allow_extras(true);
  std::string image_path_str;
  std::string mask_path_str;
  app.add_option("image", image_path_str, "Path to input image (required)")
      ->required();
  app.add_option("--mask", mask_path_str,
                 "Path to optional foreground TIFF mask");
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  aifo::pathology::config::InferenceConfig config =
      aifo::pathology::config::InferenceConfig::FromArgs(argc, argv);

  fs::path image_path(image_path_str);
  std::optional<fs::path> mask_path;
  if (!mask_path_str.empty()) {
    mask_path = fs::path(mask_path_str);
  }

  // Create and run the inference engine
  auto engine_or =
      aifo::pathology::inference_fimage::InferenceEngineFImage::Create(config);
  if (!engine_or.ok()) {
    spdlog::error("Error initializing inference engine: {}",
                  engine_or.status().message());
    return 1;
  }

  auto engine = std::move(engine_or.value());

  // Setup a spinner and attach a progress callback
  auto spinner = std::make_unique<aifocore::utilities::Spinner>();
  spinner->Start();
  engine.SetProgressCallback(
      [&spinner](int current_batch, int total_batches, int tile_index) {
        spinner->SetText("Processing batch " + std::to_string(current_batch) +
                         "/" + std::to_string(total_batches) + " (tile " +
                         std::to_string(tile_index) + ")");
      });

  absl::Status status = mask_path ? engine.ProcessImage(image_path, *mask_path)
                                  : engine.ProcessImage(image_path);
  spinner->Stop();
  if (!status.ok()) {
    spdlog::error("Error running inference: {}", status.message());
    return 1;
  }

  return 0;
}
