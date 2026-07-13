// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include "fastslide/c/registry.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "fastslide/runtime/plugin_loader.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/slide_reader.h"
#include "internal/debug.h"

// Forward declaration for slide reader wrapper creation
extern "C" {
struct FastSlideSlideReader;
FastSlideSlideReader* fastslide_slide_reader_create_from_cpp(
    std::unique_ptr<fastslide::SlideReader> reader);
}

// Buffer for last error message.
static char fastslide_last_error[1024];

// Registry wrapper
struct FastSlideRegistry {
  // The C++ registry is a singleton, so we don't store a pointer
  // This struct exists mainly for type safety
  bool initialized;

  FastSlideRegistry() : initialized(false) {}
};

// Global registry instance
static std::unique_ptr<FastSlideRegistry> global_registry;

// Error handling functions
extern "C" void fastslide_set_last_error(const char* message) {
  std::strncpy(fastslide_last_error, message, sizeof(fastslide_last_error) - 1);
  fastslide_last_error[sizeof(fastslide_last_error) - 1] = '\0';
}

const char* fastslide_get_last_error(void) {
  return fastslide_last_error;
}

void fastslide_clear_last_error(void) {
  fastslide_last_error[0] = '\0';
}

// Registry management

int fastslide_registry_initialize(void) {
  fastslide_clear_last_error();

  if (!global_registry) {
    global_registry = std::make_unique<FastSlideRegistry>();
  }

  // Explicitly register built-in formats for C API/shared library usage.
  // This ensures formats are registered even if static initialization
  // doesn't work properly (e.g. when loaded as a shared library from Go).
  auto& registry = fastslide::runtime::GetGlobalRegistry();
  auto context = fastslide::runtime::PluginLoadContext::CreateDefault();
  const auto status =
      fastslide::runtime::BuiltInPluginsInitializer::RegisterAll(registry,
                                                                 context);

  if (!status.ok()) {
    FASTSLIDE_DEBUG_PRINT(
        "[FastSlide C] WARNING: Failed to register some formats: %s\n",
        status.message().data());
    // Continue anyway - some formats may have been registered.
  }

  global_registry->initialized = true;
  return 1;
}

FastSlideRegistry* fastslide_registry_get_instance(void) {
  if (!global_registry || !global_registry->initialized) {
    fastslide_set_last_error(
        "Registry not initialized - call fastslide_registry_initialize() "
        "first");
    return nullptr;
  }
  return global_registry.get();
}

FastSlideSlideReader* fastslide_registry_create_reader(
    FastSlideRegistry* registry, const char* file_path) {
  fastslide_clear_last_error();

  if (!registry) {
    FASTSLIDE_ERROR_PRINT("[FastSlide C] ERROR: Registry is null\n");
    fastslide_set_last_error("Registry cannot be null");
    return nullptr;
  }

  if (!file_path) {
    FASTSLIDE_ERROR_PRINT("[FastSlide C] ERROR: file_path is null\n");
    fastslide_set_last_error("file_path cannot be null");
    return nullptr;
  }

  FASTSLIDE_DEBUG_PRINT("[FastSlide C] DEBUG: Creating reader for file: %s\n",
                        file_path);

  auto reader_or =
      fastslide::runtime::GetGlobalRegistry().CreateReader(file_path);
  if (!reader_or.ok()) {
    const std::string error_msg(reader_or.status().message());
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: C++ CreateReader failed for %s: %s\n", file_path,
        error_msg.c_str());
    fastslide_set_last_error(error_msg.c_str());
    return nullptr;
  }

  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: C++ CreateReader succeeded for %s\n", file_path);

  FastSlideSlideReader* wrapper =
      fastslide_slide_reader_create_from_cpp(std::move(reader_or.value()));
  if (wrapper == nullptr) {
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: Failed to create C wrapper for %s\n", file_path);
    fastslide_set_last_error("Failed to create C wrapper for reader");
    return nullptr;
  }

  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: Successfully created C wrapper for %s: %p\n",
      file_path, wrapper);
  return wrapper;
}

FastSlideSlideReader* fastslide_create_reader(const char* file_path) {
  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: fastslide_create_reader called for: %s\n",
      file_path ? file_path : "NULL");

  auto registry = fastslide_registry_get_instance();
  if (!registry) {
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: Failed to get registry instance\n");
    return nullptr;
  }
  return fastslide_registry_create_reader(registry, file_path);
}

FastSlideSlideReader* fastslide_create_reader_with_options(
    const char* file_path, const FastSlideOpenOptions* options) {
  FastSlideSlideReader* reader = fastslide_create_reader(file_path);
  if (reader == nullptr) {
    return nullptr;
  }

  if (options != nullptr && options->apply_icc) {
    if (!fastslide_slide_reader_enable_icc_transform(
            reader, options->target_color_space, options->rendering_intent,
            options->icc_use_lut)) {
      // Building the transform failed (a profile was present but unusable).
      // Surface the error and do not hand back a half-configured reader.
      fastslide_slide_reader_free(reader);
      return nullptr;
    }
  }

  return reader;
}

// Utility functions

int fastslide_registry_get_supported_extensions(FastSlideRegistry* registry,
                                                char*** extensions,
                                                int* num_extensions) {
  fastslide_clear_last_error();

  if (!registry) {
    fastslide_set_last_error("Registry cannot be null");
    return 0;
  }

  if (!extensions || !num_extensions) {
    fastslide_set_last_error("extensions and num_extensions cannot be null");
    return 0;
  }

  const auto ext_list =
      fastslide::runtime::GetGlobalRegistry().GetSupportedExtensions();
  *num_extensions = static_cast<int>(ext_list.size());

  if (*num_extensions == 0) {
    *extensions = nullptr;
    return 1;
  }

  *extensions = static_cast<char**>(malloc(*num_extensions * sizeof(char*)));
  if (*extensions == nullptr) {
    fastslide_set_last_error("Failed to allocate memory for extensions array");
    return 0;
  }

  for (int i = 0; i < *num_extensions; ++i) {
    (*extensions)[i] = static_cast<char*>(malloc(ext_list[i].length() + 1));
    if ((*extensions)[i] == nullptr) {
      for (int j = 0; j < i; ++j) {
        free((*extensions)[j]);
      }
      free(*extensions);
      *extensions = nullptr;
      fastslide_set_last_error(
          "Failed to allocate memory for extension string");
      return 0;
    }
    std::snprintf((*extensions)[i], ext_list[i].length() + 1, "%s",
                  ext_list[i].c_str());
  }

  return 1;
}

int fastslide_get_supported_extensions(char*** extensions,
                                       int* num_extensions) {
  auto registry = fastslide_registry_get_instance();
  if (!registry) {
    return 0;
  }
  return fastslide_registry_get_supported_extensions(registry, extensions,
                                                     num_extensions);
}

void fastslide_registry_free_extensions(char** extensions, int num_extensions) {
  if (!extensions || num_extensions <= 0) {
    return;
  }

  for (int i = 0; i < num_extensions; ++i) {
    free(extensions[i]);
  }
  free(extensions);
}

int fastslide_registry_is_supported(FastSlideRegistry* registry,
                                    const char* file_path) {
  fastslide_clear_last_error();

  if (!registry) {
    fastslide_set_last_error("Registry cannot be null");
    return 0;
  }

  if (!file_path) {
    fastslide_set_last_error("file_path cannot be null");
    return 0;
  }

  return fastslide::runtime::GetGlobalRegistry().CreateReader(file_path).ok()
             ? 1
             : 0;
}

int fastslide_is_supported(const char* file_path) {
  auto registry = fastslide_registry_get_instance();
  if (!registry) {
    return 0;
  }
  return fastslide_registry_is_supported(registry, file_path);
}

// Version information

const char* fastslide_get_version(void) {
  return "0.8.0";
}
