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

/// @file deb_smoke_c.c
/// @brief Standalone C smoke test for the installed FastSlide Debian packages.
///
/// This program is compiled OUTSIDE the Bazel build (see package/Dockerfile)
/// with the SYSTEM C compiler, using only the C headers shipped in
/// libfastslide-dev and linking against the libfastslide.so shipped in the
/// runtime package. It exercises the public C API: it reads the version
/// strings, initializes the library, lists the supported slide extensions via
/// the C registry path, and cleans up. A non-empty extension list proves that
/// the runtime package actually exports the fastslide_* C symbols (they are
/// only reached through FFI, so they must be force-linked into the .so), that
/// the -dev package ships C headers that compile with a C compiler, and that
/// built-in format registration works through the C API. This is the linkable
/// surface that C and Rust (fastslide-sys) consumers depend on.

#include <stdio.h>
#include <stdlib.h>

#include "fastslide/c/fastslide.h"

int main(void) {
  printf("FastSlide C API version: %s\n", fastslide_c_api_get_version());
  printf("FastSlide version: %s\n", fastslide_get_version());

  if (fastslide_initialize() == 0) {
    fprintf(stderr, "ERROR: fastslide_initialize() failed: %s\n",
            fastslide_get_last_error());
    return EXIT_FAILURE;
  }

  char** extensions = NULL;
  int num_extensions = 0;
  if (fastslide_get_supported_extensions(&extensions, &num_extensions) == 0) {
    fprintf(stderr, "ERROR: fastslide_get_supported_extensions() failed: %s\n",
            fastslide_get_last_error());
    fastslide_cleanup();
    return EXIT_FAILURE;
  }

  printf("FastSlide supports %d extension(s):\n", num_extensions);
  for (int i = 0; i < num_extensions; ++i) {
    printf("  - %s\n", extensions[i]);
  }
  fastslide_registry_free_extensions(extensions, num_extensions);

  if (num_extensions == 0) {
    fprintf(stderr,
            "ERROR: no extensions supported; the runtime package is broken.\n");
    fastslide_cleanup();
    return EXIT_FAILURE;
  }

  fastslide_cleanup();

  printf("FastSlide Debian package C API smoke test passed.\n");
  return EXIT_SUCCESS;
}
