// Copyright 2024 Jonas Teuwen. All Rights Reserved.
// Copyright 2025 Joren Brunekreef. All Rights Reserved.
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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_VIPS_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_VIPS_H_

#include <vips/vips8>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "absl/status/status.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"

namespace fs = std::filesystem;

namespace aifocore::utilities {

class VipsInitializer {
 public:
  explicit VipsInitializer(const char* argv0) {
    if (VIPS_INIT(argv0)) {
      throw std::runtime_error("Failed to initialize VIPS");
    }
    // Force single-threaded mode (1 = single thread, 0 = auto-detect)
    vips_concurrency_set(1);
  }

  ~VipsInitializer() {
    vips_shutdown();  // Ensures proper cleanup
  }

  // Delete copy constructor and assignment operator to prevent misuse
  VipsInitializer(const VipsInitializer&) = delete;
  VipsInitializer& operator=(const VipsInitializer&) = delete;
};

/**
 * @brief Save a Vips image to a file.
 *
 * This function saves a Vips image to a file. If an options pointer is
 * provided, the image will be saved with the specified options. Otherwise, the
 * image will be saved with the default options. Wrapper to
 * vips::VImage::write_to_file to separate error handling from the rest of the
 * code.
 *
 * @param image The Vips image to save.
 * @param filename The path to the file to save the image to.
 * @param options The options to use when saving the image.
 * @return An absl::Status indicating the success or failure of the operation.
 */
absl::Status SaveVipsImageToFile(const vips::VImage& image,
                                 const fs::path& filename,
                                 vips::VOption* options = nullptr) {
  // Disable VIPS threading completely
  vips_concurrency_set(0);
  try {
    if (options) {
      image.write_to_file(filename.string().c_str(), options);
    } else {
      image.write_to_file(filename.string().c_str());
    }
    return absl::OkStatus();
  } catch (const vips::VError& e) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       aifocore::fmt::format("VIPS error: {}", e.what()));
  } catch (const std::exception& e) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       aifocore::fmt::format("Unexpected error: {}", e.what()));
  } catch (...) {
    return MAKE_STATUS(absl::StatusCode::kInternal, "Unknown exception");
  }
}

}  // namespace aifocore::utilities

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_VIPS_H_
