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
#include <memory>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "readers/isyntax/third_party/isyntax.h"
#include "readers/isyntax/third_party/seektable.h"

namespace isyntax {

aifocore::Status BuildSeekTable(std::FILE* fp, isyntax_t* isyntax,
                                isyntax_image_t* wsi_image) {
  if (!fp || !isyntax || !wsi_image) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "BuildSeekTable: null argument");
  }

  isyntax_dicom_tag_header_t seektable_header_tag = {0};
  (void)aifocore::portable_fread(&seektable_header_tag,
                                 sizeof(isyntax_dicom_tag_header_t), fp);

  if (seektable_header_tag.group != 0x301D ||
      seektable_header_tag.element != 0x2015) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "BuildSeekTable: unexpected seektable header tag");
  }

  int32_t seektable_size = static_cast<int32_t>(seektable_header_tag.size);
  if (seektable_size < 0) {
    // We need to guess the size...
    ASSERT(wsi_image->codeblock_count > 0);
    seektable_size = sizeof(isyntax_seektable_codeblock_header_t) *
                     wsi_image->codeblock_count;
  }

  using MallocPtrSeek = std::unique_ptr<isyntax_seektable_codeblock_header_t,
                                        decltype(&std::free)>;
  MallocPtrSeek seektable(static_cast<isyntax_seektable_codeblock_header_t*>(
                              std::malloc(seektable_size)),
                          &std::free);
  if (!seektable) {
    return aifocore::Status(aifocore::StatusCode::kResourceExhausted,
                            "BuildSeekTable: malloc failed");
  }

  isyntax_seektable_codeblock_header_t* seektable_raw = seektable.get();
  (void)aifocore::portable_fread(seektable_raw, seektable_size, fp);

  // NOTE: The number of codeblock entries in the seektable is much
  // greater than the number of codeblocks that actually exist in the file.
  // Many entries have offset/size == 0.
  const int32_t seektable_entry_count =
      seektable_size /
      static_cast<int32_t>(sizeof(isyntax_seektable_codeblock_header_t));

  for (int32_t i = 0; i < wsi_image->codeblock_count; ++i) {
    isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
    if (codeblock->block_id > seektable_entry_count) {
      ASSERT(!"block ID out of bounds");
      return aifocore::Status(aifocore::StatusCode::kOutOfRange,
                              "BuildSeekTable: block_id out of bounds");
    }
    isyntax_seektable_codeblock_header_t* seektable_entry =
        seektable_raw + codeblock->block_id;
    ASSERT(seektable_entry->block_data_offset_header.group == 0x301D);
    ASSERT(seektable_entry->block_data_offset_header.element == 0x2010);
    codeblock->block_data_offset = seektable_entry->block_data_offset;
    codeblock->block_size = seektable_entry->block_size;
  }

  return aifocore::Status();
}

}  // namespace isyntax
