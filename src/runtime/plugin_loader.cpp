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

#include "fastslide/runtime/plugin_loader.h"

#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/readers.h"
#include "fastslide/runtime/reader_registry.h"

namespace fastslide {
namespace runtime {

std::vector<FormatDescriptor> BuiltInPluginsInitializer::GetDescriptors() {
  return readers::GetBuiltinFormats();
}

aifocore::Status BuiltInPluginsInitializer::RegisterAll(
    ReaderRegistry& registry) {
  auto descriptors = GetDescriptors();
  for (auto& descriptor : descriptors) {
    registry.RegisterFormat(std::move(descriptor));
  }
  return aifocore::Status::OkStatus();
}

}  // namespace runtime
}  // namespace fastslide
