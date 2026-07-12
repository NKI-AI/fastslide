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

/// @file deb_smoke.cpp
/// @brief Standalone smoke test for the installed FastSlide Debian packages.
///
/// This program is compiled OUTSIDE the Bazel build (see package/Dockerfile),
/// using only the headers shipped in libfastslide-dev and linking against the
/// libfastslide.so shipped in the runtime package. It exercises the public C++
/// API by initializing the global reader registry and listing the built-in
/// slide formats. A non-empty format list proves that the shared library, its
/// statically linked dependencies, and the built-in format registration all
/// load correctly from the installed package, and that the -dev package ships
/// every header needed to compile against the C++ SDK.

#include <cstdlib>
#include <iostream>

#include "fastslide/runtime/reader_registry.h"

int main() {
  const auto formats = fastslide::GetGlobalRegistry().ListFormats();

  std::cout << "FastSlide registered " << formats.size() << " format(s):\n";
  for (const auto& format : formats) {
    std::cout << "  - " << format << '\n';
  }

  if (formats.empty()) {
    std::cerr
        << "ERROR: no formats registered; the runtime package is broken.\n";
    return EXIT_FAILURE;
  }

  std::cout << "FastSlide Debian package smoke test passed.\n";
  return EXIT_SUCCESS;
}
