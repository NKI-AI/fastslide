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

#include "fastslide/readers/isyntax/third_party/open.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "fastslide/readers/isyntax/third_party/open_helpers.h"
#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/seektable.h"
#include "fastslide/readers/isyntax/third_party/xml_parser.h"

void populate_tile_debug_info(isyntax_image_t* wsi_image);

namespace isyntax {

std::string BuildXmlDumpFilename(std::string_view input_filename) {
  std::string out(input_filename);

  constexpr std::string_view kExt = ".isyntax";
  size_t dot = out.find_last_of('.');
  if (dot != std::string::npos && std::string_view(out).substr(dot) == kExt) {
    out.resize(dot);
    out.append(".xml");
  } else {
    out.append(".xml");
  }

  for (char& c : out) {
    if (c == ' ') {
      c = '_';
    }
  }
  return out;
}

namespace {

class FileStreamGuard {
 public:
  explicit FileStreamGuard(std::FILE* fp) : fp_(fp) {}

  ~FileStreamGuard() {
    if (fp_) {
      (void)aifocore::portable_fclose(fp_);
    }
  }

  FileStreamGuard(const FileStreamGuard&) = delete;
  FileStreamGuard& operator=(const FileStreamGuard&) = delete;

  std::FILE* get() const { return fp_; }

 private:
  std::FILE* fp_ = nullptr;
};

bool ShouldTreatOpenFailureAsSuccess(const isyntax_t* isyntax) {
  if (!isyntax) {
    return false;
  }
  return (isyntax->open_flags & ISYNTAX_OPEN_FLAG_READ_BARCODE_ONLY) &&
         isyntax->is_barcode_read;
}

struct XmlHeaderInfo {
  int64_t header_length = 0;
  int64_t data_offset = 0;
};

aifocore::Result<XmlHeaderInfo> ReadXmlHeader(std::FILE* fp, isyntax_t* isyntax,
                                              std::string_view filename) {
  if (fp == nullptr || isyntax == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "ReadXmlHeader: null argument");
  }

  // Match the original chunked scan behavior.
  constexpr size_t kReadSize = MEGABYTES(1);
  using MallocPtrChar = std::unique_ptr<char, decltype(&std::free)>;
  MallocPtrChar read_buffer_ptr(static_cast<char*>(std::malloc(kReadSize)),
                                &std::free);
  char* read_buffer = read_buffer_ptr.get();
  if (read_buffer == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                            "ReadXmlHeader: failed to allocate read buffer");
  }

  // Optional XML dump output (best-effort).
  std::FILE* xml_dump_fp = nullptr;
  if (isyntax->open_flags & ISYNTAX_OPEN_FLAG_DUMP_XML_HEADER) {
    const std::string xml_filename = BuildXmlDumpFilename(filename);
    xml_dump_fp = aifocore::portable_fopen(xml_filename, "wb");
    if (xml_dump_fp != nullptr) {
      isyntax->xml_dump_file = xml_dump_fp;
    }
  }

  auto CloseXmlDump = [&]() {
    if (xml_dump_fp != nullptr) {
      (void)aifocore::portable_fclose(xml_dump_fp);
      xml_dump_fp = nullptr;
      isyntax->xml_dump_file = nullptr;
    }
  };

  size_t bytes_read = aifocore::portable_fread(read_buffer, kReadSize, fp);
  if (bytes_read < 3) {
    CloseXmlDump();
    return aifocore::Status(aifocore::StatusCode::kDataLoss,
                            "ReadXmlHeader: file too small");
  }
  bool are_there_bytes_left = (bytes_read == kReadSize);

  int64_t header_length = 0;
  int64_t data_offset = 0;

  for (int32_t chunk_index = 0;; ++chunk_index) {
    const int64_t chunk_offset =
        static_cast<int64_t>(chunk_index) * static_cast<int64_t>(kReadSize);

    const void* marker =
        std::memchr(read_buffer, '\x04', static_cast<int>(bytes_read));
    if (marker != nullptr) {
      const int64_t marker_offset =
          static_cast<const char*>(marker) - read_buffer;
      const int64_t chunk_length = marker_offset;
      header_length += chunk_length;
      data_offset = header_length + 1;

      if (!(header_length > 0 && header_length < isyntax->filesize)) {
        CloseXmlDump();
        return aifocore::Status(aifocore::StatusCode::kDataLoss,
                                "ReadXmlHeader: invalid header length");
      }

      aifocore::Status st = isyntax::ParseXmlHeaderChunk(
          isyntax,
          std::span<const char>(read_buffer, static_cast<size_t>(chunk_length)),
          static_cast<int64_t>(chunk_offset), /*is_last_chunk=*/true);
      CloseXmlDump();
      if (!st.ok()) {
        return st;
      }
      return XmlHeaderInfo{.header_length = header_length,
                           .data_offset = data_offset};
    }

    // Marker not found yet; continue scanning.
    header_length += static_cast<int64_t>(kReadSize);
    if (!are_there_bytes_left) {
      CloseXmlDump();
      return aifocore::Status(
          aifocore::StatusCode::kDataLoss,
          "ReadXmlHeader: didn't find end-of-header marker before EOF");
    }

    if (xml_dump_fp != nullptr) {
      (void)aifocore::portable_fwrite(read_buffer, kReadSize, xml_dump_fp);
    }

    aifocore::Status st = isyntax::ParseXmlHeaderChunk(
        isyntax, std::span<const char>(read_buffer, kReadSize),
        static_cast<int64_t>(chunk_offset), /*is_last_chunk=*/false);
    if (!st.ok()) {
      CloseXmlDump();
      return st;
    }

    bytes_read = aifocore::portable_fread(read_buffer, kReadSize, fp);
    are_there_bytes_left = (bytes_read == kReadSize);
  }
}

aifocore::Status InitializeLevelGeometry(isyntax_t* isyntax,
                                         isyntax_image_t* wsi_image) {
  isyntax::open::InitializeLevelGeometry(isyntax, wsi_image);
  return aifocore::Status();
}

aifocore::Status ProcessCodeBlocks(isyntax_t* isyntax,
                                   isyntax_image_t* wsi_image) {
  isyntax::open::ProcessCodeBlocks(isyntax, wsi_image);
  return aifocore::Status();
}

aifocore::Status ReadSeektable(std::FILE* fp, isyntax_t* isyntax,
                               isyntax_image_t* wsi_image) {
  return isyntax::BuildSeekTable(fp, isyntax, wsi_image);
}

aifocore::Status CreateTileLookupTables(isyntax_t* isyntax,
                                        isyntax_image_t* wsi_image) {
  return isyntax::open::CreateTileLookupTables(isyntax, wsi_image);
}

aifocore::Status InitializeBlockAllocators(isyntax_t* isyntax,
                                           isyntax_open_flags_t flags) {
  return isyntax::open::InitializeBlockAllocators(isyntax, flags);
}

aifocore::Status InitializeDummyBlocks(isyntax_t* isyntax) {
  return isyntax::open::InitializeDummyBlocks(isyntax);
}

aifocore::Status PopulateTileDebugInfo(isyntax_image_t* wsi_image) {
  populate_tile_debug_info(wsi_image);
  return aifocore::Status();
}

aifocore::Status RunOpenPipeline(isyntax_t* isyntax, const char* filename,
                                 isyntax_open_flags_t flags) {
  if (!isyntax || !filename) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "isyntax_open_cpp: null argument");
  }

  // Preserve previous behavior: `isyntax_open` sets these early.
  isyntax->open_flags = flags;
  isyntax->xml_dump_file = NULL;
  isyntax->xml_paranoid_mode = true;
  isyntax->xml_cpp_state = NULL;
  isyntax->file_handle = 0;

  std::FILE* fp = aifocore::portable_fopen(filename, "rb");
  FileStreamGuard fp_guard(fp);
  if (!fp) {
    return aifocore::Status(aifocore::StatusCode::kNotFound,
                            "Failed to open iSyntax file");
  }

  int64_t filesize = static_cast<int64_t>(aifocore::portable_filesize(fp));
  if (filesize <= 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid iSyntax filesize");
  }
  isyntax->filesize = filesize;

  XmlHeaderInfo header;
  AIFOCORE_ASSIGN_OR_RETURN(header, ReadXmlHeader(fp, isyntax, filename));

  isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;
  if (wsi_image->image_type != ISYNTAX_IMAGE_TYPE_WSI) {
    return aifocore::Status(aifocore::StatusCode::kUnimplemented,
                            "Non-WSI images are not supported");
  }

  AIFOCORE_RETURN_IF_ERROR(InitializeLevelGeometry(isyntax, wsi_image));
  AIFOCORE_RETURN_IF_ERROR(ProcessCodeBlocks(isyntax, wsi_image));

  (void)aifocore::portable_fseek(fp, header.data_offset, SEEK_SET);
  if (wsi_image->header_codeblocks_are_partial) {
    AIFOCORE_RETURN_IF_ERROR(ReadSeektable(fp, isyntax, wsi_image));
    AIFOCORE_RETURN_IF_ERROR(CreateTileLookupTables(isyntax, wsi_image));
  } else if (isyntax->data_model_major_version >= 100) {
    AIFOCORE_RETURN_IF_ERROR(CreateTileLookupTables(isyntax, wsi_image));
  } else {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                                "Non-partial header blocks unsupported");
  }

  AIFOCORE_RETURN_IF_ERROR(InitializeBlockAllocators(isyntax, flags));
  AIFOCORE_RETURN_IF_ERROR(InitializeDummyBlocks(isyntax));
  AIFOCORE_RETURN_IF_ERROR(PopulateTileDebugInfo(wsi_image));

  isyntax->file_handle = aifocore::portable_open(filename, O_RDONLY | O_BINARY);
  if (!isyntax->file_handle) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Could not reopen file for asynchronous I/O");
  }

  return aifocore::Status();
}

}  // namespace

}  // namespace isyntax

namespace isyntax {

aifocore::Status OpenIsyntaxFile(isyntax_t* isyntax, const char* filename,
                                 isyntax_open_flags_t flags) {
  aifocore::Status st = RunOpenPipeline(isyntax, filename, flags);
  if (st.ok()) {
    return st;
  }

  // Ensure callers can safely destroy the handle even after a partial open.
  CleanupPartialIsyntax(isyntax);

  // Preserve previous behavior: barcode-only mode may intentionally abort early
  // and still be considered successful.
  if (ShouldTreatOpenFailureAsSuccess(isyntax)) {
    return aifocore::Status();
  }
  return st;
}

}  // namespace isyntax

namespace isyntax {

void CleanupPartialIsyntax(isyntax_t* isyntax) {
  if (isyntax == nullptr) {
    return;
  }

  // XML parser state and XML dump output.
  if (isyntax->xml_cpp_state) {
    isyntax::DestroyXmlCppState(isyntax);
  }
  if (isyntax->xml_dump_file) {
    std::fclose(isyntax->xml_dump_file);
    isyntax->xml_dump_file = nullptr;
  }

  // Allocators + dummy blocks (may or may not be initialized).
  if (isyntax->is_block_allocator_owned) {
    if (isyntax->ll_coeff_block_allocator &&
        isyntax->ll_coeff_block_allocator->is_valid) {
      block_allocator_destroy(isyntax->ll_coeff_block_allocator);
    }
    if (isyntax->h_coeff_block_allocator &&
        isyntax->h_coeff_block_allocator->is_valid) {
      block_allocator_destroy(isyntax->h_coeff_block_allocator);
    }
  }

  if (isyntax->black_dummy_coeff) {
    std::free(isyntax->black_dummy_coeff);
    isyntax->black_dummy_coeff = nullptr;
  }
  if (isyntax->white_dummy_coeff) {
    std::free(isyntax->white_dummy_coeff);
    isyntax->white_dummy_coeff = nullptr;
  }

  // Images (WSI and any partially populated state).
  for (int32_t image_index = 0;
       image_index < static_cast<int32_t>(sizeof(isyntax->images) /
                                          sizeof(isyntax->images[0]));
       ++image_index) {
    isyntax_image_t* image = isyntax->images + image_index;
    if (image->codeblocks) {
      std::free(image->codeblocks);
      image->codeblocks = nullptr;
      image->codeblock_count = 0;
    }
    if (image->data_chunks) {
      for (int32_t i = 0; i < image->data_chunk_count; ++i) {
        isyntax_data_chunk_t* chunk = image->data_chunks + i;
        if (chunk->data) {
          std::free(chunk->data);
          chunk->data = nullptr;
        }
      }
      std::free(image->data_chunks);
      image->data_chunks = nullptr;
      image->data_chunk_count = 0;
    }
    for (int32_t i = 0; i < image->level_count; ++i) {
      isyntax_level_t* level = image->levels + i;
      if (level->tiles) {
        std::free(level->tiles);
        level->tiles = nullptr;
      }
    }
  }

  // Cache lifetime is managed by the C++ layer (`isyntax::IsyntaxCache`).
  isyntax->cache = nullptr;

  // Async file handle opened at the end of successful open.
  if (isyntax->file_handle) {
    (void)aifocore::portable_close(isyntax->file_handle);
    isyntax->file_handle = 0;
  }
}

void DestroyIsyntax(isyntax_t* isyntax) {
  CleanupPartialIsyntax(isyntax);
}

}  // namespace isyntax
