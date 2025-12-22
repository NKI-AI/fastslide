//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE

#pragma once

#include <stdint.h>

// NOTE: This header replaces the legacy public C API header (`libisyntax.h`)
// after we remove that API. These types are still used across the internal C
// and C++ translation units.

enum isyntax_pixel_format_t {
  _ISYNTAX_PIXEL_FORMAT_START = 0x100,
  ISYNTAX_PIXEL_FORMAT_RGBA,
  ISYNTAX_PIXEL_FORMAT_BGRA,
  _ISYNTAX_PIXEL_FORMAT_END,
};

typedef int32_t isyntax_open_flags_t;

enum isyntax_open_flags_enum {
  // Initialize internal coefficient allocators during open.
  ISYNTAX_OPEN_FLAG_INIT_ALLOCATORS = 1,

  // Only read barcode then abort early (still treated as success).
  ISYNTAX_OPEN_FLAG_READ_BARCODE_ONLY = 2,

  // Dump the raw XML header to a .xml file (debug).
  ISYNTAX_OPEN_FLAG_DUMP_XML_HEADER = 4,
};
