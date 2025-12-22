//  This file is derived from libisyntax.

//  Original work:
//  Copyright (c) 2019-2024, Pieter Valkema
//  Licensed under the BSD 2-Clause License.

//  Modifications and C++ port:
//  Copyright (c) 2025, Jonas Teuwen

//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:

//  1. Redistributions of source code must retain the above copyright notice,
//  this
//     list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE

#pragma once

#include <string>
#include <string_view>

#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/isyntax_types.h"

namespace aifocore {
class Status;
}  // namespace aifocore

namespace isyntax {

// Pure helper used by `isyntax_open` (and unit-tested).
std::string BuildXmlDumpFilename(std::string_view input_filename);

// C++ entrypoint for the open pipeline with rich error reporting.
aifocore::Status OpenIsyntaxFile(isyntax_t* isyntax, const char* filename,
                                 isyntax_open_flags_t flags);

// Cleanup helper for partially-initialized `isyntax_t` during open failures.
void CleanupPartialIsyntax(isyntax_t* isyntax);

// Full teardown for an initialized (or partially initialized) `isyntax_t`.
void DestroyIsyntax(isyntax_t* isyntax);

}  // namespace isyntax
