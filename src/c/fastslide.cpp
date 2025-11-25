// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include "fastslide/c/fastslide.h"

#include <cstdio>   // Required for printf
#include <cstring>  // Required for strlen
#include <string>

// C API version functions

const char* fastslide_c_api_get_version(void) {
  return "1.0.0";
}

int fastslide_initialize(void) {
  int result = fastslide_registry_initialize();
  if (result) {
  } else {
    const char* error = fastslide_get_last_error();
    if (error && strlen(error) > 0) {
      printf("[FastSlide C] ERROR: Initialization error: %s\n", error);
    }
  }
  return result;
}

void fastslide_cleanup(void) {
  // Clear any error state
  fastslide_clear_last_error();

  // Note: C++ destructors will handle cleanup automatically
  // when global objects go out of scope
}
