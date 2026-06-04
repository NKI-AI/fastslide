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
//
// Original C code:
//   BSD 2-Clause License
//   Copyright (c) 2019-2025, Pieter Valkema

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>

#include "aifocore/platform/portability.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/xml_parser.h"

bool read_xml_header(std::FILE* file_stream, isyntax_t* isyntax,
                     int64_t* out_header_length, int64_t* out_data_offset) {
  size_t read_size = MEGABYTES(1);
  using MallocPtrChar = std::unique_ptr<char, decltype(&std::free)>;
  MallocPtrChar read_buffer_ptr(static_cast<char*>(std::malloc(read_size)),
                                &std::free);
  char* read_buffer = read_buffer_ptr.get();
  if (!read_buffer) {
    return false;
  }

  size_t bytes_read =
      aifocore::portable_fread(read_buffer, read_size, file_stream);
  if (bytes_read < 3) {
    return false;
  }
  bool are_there_bytes_left = (bytes_read == read_size);

  // Find EOT candidate byte '\x04'.
  int64_t header_length = 0;
  int64_t isyntax_data_offset = 0;

  // For XML dump (if flag is set).
  FILE* xml_dump_fp = nullptr;
  if (isyntax->open_flags & ISYNTAX_OPEN_FLAG_DUMP_XML_HEADER) {
    xml_dump_fp = isyntax->xml_dump_file;
  }

  int32_t chunk_index = 0;
  for (;; ++chunk_index) {
    int64_t chunk_offset = chunk_index * static_cast<int64_t>(read_size);
    int64_t chunk_length = 0;
    bool match = false;
    char* marker =
        static_cast<char*>(std::memchr(read_buffer, '\x04', bytes_read));
    if (marker) {
      int64_t marker_offset = static_cast<int64_t>(marker - read_buffer);
      match = true;
      chunk_length = marker_offset;
      header_length += chunk_length;
      isyntax_data_offset = header_length + 1;
    }

    if (match) {
      if (!(header_length > 0 && header_length < isyntax->filesize)) {
        return false;
      }

      if (xml_dump_fp) {
        fwrite(read_buffer, 1, chunk_length, xml_dump_fp);
      }

      if (!isyntax::ParseXmlHeaderChunk(
               isyntax,
               std::span<const char>(read_buffer,
                                     static_cast<size_t>(chunk_length)),
               static_cast<int64_t>(chunk_offset), /*is_last_chunk=*/true)
               .ok()) {
        return false;
      }
      break;
    }

    // Not found yet; continue scanning.
    chunk_length = static_cast<int64_t>(read_size);
    header_length += chunk_length;
    if (are_there_bytes_left) {
      if (xml_dump_fp) {
        fwrite(read_buffer, 1, static_cast<size_t>(chunk_length), xml_dump_fp);
      }

      if (!isyntax::ParseXmlHeaderChunk(
               isyntax,
               std::span<const char>(read_buffer,
                                     static_cast<size_t>(chunk_length)),
               static_cast<int64_t>(chunk_offset), /*is_last_chunk=*/false)
               .ok()) {
        return false;
      }

      bytes_read =
          aifocore::portable_fread(read_buffer, read_size, file_stream);
      are_there_bytes_left = (bytes_read == read_size);
      continue;
    }

    return false;
  }

  if (xml_dump_fp) {
    fclose(xml_dump_fp);
    isyntax->xml_dump_file = nullptr;
  }

  *out_header_length = header_length;
  *out_data_offset = isyntax_data_offset;
  return true;
}
