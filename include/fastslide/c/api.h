// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_API_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_API_H_

/// @file api.h
/// @brief Symbol visibility / DLL-export macro for the FastSlide C API.
///
/// On Windows, DLL symbols must be explicitly exported with
/// `__declspec(dllexport)`. Without this, `GetProcAddress` (and JNI/JNA
/// `Library.find_symbol`) cannot resolve the C API entry points even though
/// the implementation is present in the DLL.
///
/// Behavior:
/// - When building the FastSlide shared library, define
///   `FASTSLIDE_BUILDING_DLL`. The Bazel DLL targets do this automatically via
///   `local_defines`. Function declarations then expand to
///   `__declspec(dllexport)` on Windows and
///   `__attribute__((visibility("default")))` on Unix-like systems.
/// - When consuming FastSlide as a DLL on Windows and you want the linker to
///   emit slightly better import-thunk codegen, define `FASTSLIDE_USING_DLL`
///   in the consumer's compile flags. This expands declarations to
///   `__declspec(dllimport)`.
/// - In all other cases (including static linking, which is the default for
///   most internal consumers), declarations expand to nothing — identical to
///   the historical behavior.
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(FASTSLIDE_BUILDING_DLL)
#define FASTSLIDE_API __declspec(dllexport)
#elif defined(FASTSLIDE_USING_DLL)
#define FASTSLIDE_API __declspec(dllimport)
#else
#define FASTSLIDE_API
#endif
#else
#if defined(FASTSLIDE_BUILDING_DLL) && (defined(__GNUC__) || defined(__clang__))
#define FASTSLIDE_API __attribute__((visibility("default")))
#else
#define FASTSLIDE_API
#endif
#endif

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_C_API_H_
