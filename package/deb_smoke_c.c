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
/// with a pure C compiler (gcc -std=c11, NOT g++), so it doubles as proof that
/// the shipped fastslide/c/*.h headers are C-clean and thus usable by tools
/// like bindgen. It links only against the installed libfastslide.so and calls
/// the public C API: a passing run proves the runtime package actually exports
/// the fastslide_* C symbols (the whole point of a C ABI for Rust / ctypes /
/// other FFI consumers), which the C++-only deb_smoke.cpp does not exercise.

#include <stdio.h>
#include <stdlib.h>

#include "fastslide/c/fastslide.h"

int main(void) {
  // The C API returns 1 on success, 0 on failure.
  if (fastslide_initialize() != 1) {
    const char* error = fastslide_get_last_error();
    fprintf(stderr, "ERROR: fastslide_initialize() failed: %s\n",
            error != NULL ? error : "(no error message)");
    return EXIT_FAILURE;
  }

  printf("FastSlide C API version: %s\n", fastslide_c_api_get_version());
  printf("FastSlide library version: %s\n", fastslide_get_version());

  char** extensions = NULL;
  int num_extensions = 0;
  if (fastslide_get_supported_extensions(&extensions, &num_extensions) != 1) {
    const char* error = fastslide_get_last_error();
    fprintf(stderr, "ERROR: fastslide_get_supported_extensions() failed: %s\n",
            error != NULL ? error : "(no error message)");
    fastslide_cleanup();
    return EXIT_FAILURE;
  }

  printf("FastSlide supports %d file extension(s):\n", num_extensions);
  for (int i = 0; i < num_extensions; ++i) {
    printf("  - %s\n", extensions[i]);
  }

  const int had_extensions = num_extensions > 0;
  fastslide_registry_free_extensions(extensions, num_extensions);

  if (!had_extensions) {
    fprintf(stderr,
            "ERROR: no extensions registered; the runtime package is broken.\n");
    fastslide_cleanup();
    return EXIT_FAILURE;
  }

  fastslide_cleanup();
  printf("FastSlide Debian package C smoke test passed.\n");
  return EXIT_SUCCESS;
}
