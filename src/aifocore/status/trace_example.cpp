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
#include <iostream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"

// Function at the deepest level that generates an error
absl::StatusOr<std::string> ReadConfigFile(const std::string& file_path,
                                           bool simulate_error) {
  if (simulate_error) {
    // <-- USE MAKE_STATUSOR for StatusOr<T> returns
    return MAKE_STATUSOR(std::string, absl::StatusCode::kNotFound,
                         "File not found: " + file_path);
  }
  return "Configuration data loaded successfully";
}

// Mid-level function that processes the config
absl::StatusOr<int> ParseConfigValue(const std::string& file_path,
                                     const std::string& key,
                                     bool simulate_error) {
  std::string config_content;
  ASSIGN_OR_RETURN(config_content, ReadConfigFile(file_path, simulate_error),
                   "Failed to read configuration file");

  if (key.empty()) {
    // <-- MAKE_STATUSOR again
    return MAKE_STATUSOR(int, absl::StatusCode::kInvalidArgument,
                         "Empty key specified");
  }

  return 42;
}

// Higher level functions stay the same…

// Higher level function that uses the config value
absl::Status InitializeSystem(const std::string& config_path,
                              bool simulate_error) {
  int config_value;
  ASSIGN_OR_RETURN(
      config_value,
      ParseConfigValue(config_path, "database.port", simulate_error),
      "Failed to parse configuration");

  // Use the config value
  std::cout << "System initialized with config value: " << config_value
            << std::endl;
  return absl::OkStatus();
}

// Top-level function that would be called by main
absl::Status StartApplication(const std::string& config_path,
                              bool simulate_error) {
  absl::Status status = InitializeSystem(config_path, simulate_error);
  if (!status.ok()) {
    std::cerr << "Application failed to start with error:" << std::endl;
    std::cerr << status.message() << std::endl;
  }

  return status;
}

// Main function to run the example
int main(int argc, char** argv) {
  bool simulate_error = true;
  std::string config_path = "/etc/myapp/config.json";

  if (argc > 1 && std::string(argv[1]) == "--no-error") {
    simulate_error = false;
  }

  absl::Status result = StartApplication(config_path, simulate_error);
  return result.ok() ? 0 : 1;
}
