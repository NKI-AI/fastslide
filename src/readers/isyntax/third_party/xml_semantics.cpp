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

#include "fastslide/readers/isyntax/third_party/xml_semantics.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/base64.h"
#include "fastslide/readers/isyntax/third_party/chunk_layout.h"
#include "fastslide/readers/isyntax/third_party/utils/math_utils.h"

namespace {

constexpr int kPerLevelPadding = 3;

// Helper function to parse DICOM group 0x0008 elements.
aifocore::Status ParseGroup0008(uint32_t element, char* value,
                                uint64_t value_len) {
  static_cast<void>(value);
  static_cast<void>(value_len);
  switch (element) {
    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format(
              "Unknown element (group=0x0008, element=0x{:04x})", element));
    case 0x002A: /*DICOM_ACQUISITION_DATETIME*/ {
    } break;
    case 0x0070: /*DICOM_MANUFACTURER*/ {
    } break;
    case 0x1090: /*DICOM_MANUFACTURERS_MODEL_NAME*/ {
    } break;
    case 0x2111: /*DICOM_DERIVATION_DESCRIPTION*/ {
      // "PHILIPS UFS V%s | Quality=%d | DWT=%d | Compressor=%d"
    } break;
  }
  return aifocore::Status::OkStatus();
}

// Helper function to parse DICOM group 0x0028 elements.
aifocore::Status ParseGroup0028(isyntax_t* isyntax, isyntax_image_t* image,
                                uint32_t element, char* value,
                                uint64_t value_len) {
  static_cast<void>(value);
  switch (element) {
    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format(
              "Unknown element (group=0x0028, element=0x{:04x})", element));
    case 0x0002: /*DICOM_SAMPLES_PER_PIXEL*/ {
    } break;
    case 0x0100: /*DICOM_BITS_ALLOCATED*/ {
    } break;
    case 0x0101: /*DICOM_BITS_STORED*/ {
    } break;
    case 0x0102: /*DICOM_HIGH_BIT*/ {
    } break;
    case 0x0103: /*DICOM_PIXEL_REPRESENTATION*/ {
    } break;
    case 0x2000: /*DICOM_ICCPROFILE*/ {
      // TODO(jonasteuwen): Our base64 decoding might not need this
      if (value_len > 0) {
        char last_char = '\0';
        if (isyntax->parser.content_was_skipped &&
            isyntax->parser.content_last_char_valid) {
          last_char = isyntax->parser.content_last_char;
        } else {
          last_char = value[value_len - 1];
        }
        if (last_char == '/') {
          value_len--;  // Trailing '/' may break base64 decode.
        }
      }
      image->base64_encoded_icc_profile_file_offset =
          isyntax->parser.content_file_offset;
      image->base64_encoded_icc_profile_len = value_len;
    } break;
    case 0x2110: /*DICOM_LOSSY_IMAGE_COMPRESSION*/ {
    } break;
    case 0x2112: /*DICOM_LOSSY_IMAGE_COMPRESSION_RATIO*/ {
    } break;
    case 0x2114: /*DICOM_LOSSY_IMAGE_COMPRESSION_METHOD*/ {
    } break;  // "PHILIPS_DP_1_0"
  }
  return aifocore::Status::OkStatus();
}

// Helper function to parse UFS_IMAGE_BLOCK_HEADER_TABLE.
bool ParseBlockHeaderTable(isyntax_image_t* image, char* value,
                           uint64_t value_len) {
  auto decoded_res = isyntax::Base64Decode(std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(value), value_len));
  if (!decoded_res.ok()) {
    return false;
  }
  std::vector<uint8_t> decoded = std::move(decoded_res).value();
  if (decoded.size() < 4) {
    return false;
  }

  uint8_t* decoded_raw = decoded.data();
  uint32_t header_size = *reinterpret_cast<uint32_t*>(decoded_raw);
  uint8_t* block_header_start = decoded_raw + 4;
  isyntax_dicom_tag_header_t sequence_element =
      *reinterpret_cast<isyntax_dicom_tag_header_t*>(block_header_start);

  if (sequence_element.size == 40) {
    // Partial header structure (without 'Block Data Offset' and 'Block Size').
    uint32_t block_count = header_size / 48;
    uint32_t should_be_zero = header_size % 48;
    if (should_be_zero != 0) {
      return false;
    }

    image->codeblock_count = block_count;
    image->codeblocks = static_cast<isyntax_codeblock_t*>(
        calloc(1, block_count * sizeof(isyntax_codeblock_t)));
    image->header_codeblocks_are_partial = true;

    for (int32_t i = 0; i < static_cast<int32_t>(block_count); ++i) {
      isyntax_partial_block_header_t* header =
          reinterpret_cast<isyntax_partial_block_header_t*>(
              block_header_start) +
          i;
      isyntax_codeblock_t* codeblock = image->codeblocks + i;
      codeblock->x_coordinate = header->x_coordinate;
      codeblock->y_coordinate = header->y_coordinate;
      codeblock->color_component = header->color_component;
      codeblock->scale = header->scale;
      codeblock->coefficient = header->coefficient;
      codeblock->block_header_template_id = header->block_header_template_id;
    }
    return true;
  }

  if (sequence_element.size == 72) {
    // Complete header structure.
    uint32_t block_count = header_size / 80;
    uint32_t should_be_zero = header_size % 80;
    if (should_be_zero != 0) {
      return false;
    }

    image->codeblock_count = block_count;
    image->codeblocks = static_cast<isyntax_codeblock_t*>(
        calloc(1, block_count * sizeof(isyntax_codeblock_t)));
    image->header_codeblocks_are_partial = false;

    for (int32_t i = 0; i < static_cast<int32_t>(block_count); ++i) {
      isyntax_full_block_header_t* header =
          reinterpret_cast<isyntax_full_block_header_t*>(block_header_start) +
          i;
      isyntax_codeblock_t* codeblock = image->codeblocks + i;
      codeblock->x_coordinate = header->x_coordinate;
      codeblock->y_coordinate = header->y_coordinate;
      codeblock->color_component = header->color_component;
      codeblock->scale = header->scale;
      codeblock->coefficient = header->coefficient;
      codeblock->block_data_offset = header->block_data_offset;
      codeblock->block_size = header->block_size;
      codeblock->block_header_template_id = header->block_header_template_id;
    }
    return true;
  }

  return false;
}

// Helper function to parse UFS_IMAGE_CLUSTER_HEADER_TABLE.
bool ParseClusterHeaderTable(isyntax_t* isyntax, isyntax_image_t* image,
                             char* value, uint64_t value_len) {
  auto decoded_res = isyntax::Base64Decode(std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(value), value_len));
  if (!decoded_res.ok()) {
    return false;
  }
  std::vector<uint8_t> decoded = std::move(decoded_res).value();
  if (decoded.size() < 4) {
    return false;
  }

  uint8_t* decoded_raw = decoded.data();
  uint8_t* decoded_end = decoded_raw + decoded.size();
  static_cast<void>(decoded_end);
  uint32_t header_size = *reinterpret_cast<uint32_t*>(decoded_raw);
  static_cast<void>(header_size);
  uint8_t* block_header_start = decoded_raw + 4;
  uint8_t* pos = block_header_start;
  isyntax_dicom_tag_header_t sequence_element =
      *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);

  // first element should be a sequence tag
  if (!(sequence_element.group == 0xfffe &&
        sequence_element.element == 0xe000)) {
    return false;
  }

  // pass 1: check how many clusters there are
  int32_t cluster_count = 1;
  for (;;) {
    uint8_t* next_sequence_element_pos =
        pos + sizeof(isyntax_dicom_tag_header_t) +
        reinterpret_cast<isyntax_dicom_tag_header_t*>(pos)->size;
    isyntax_dicom_tag_header_t* next_sequence_element =
        reinterpret_cast<isyntax_dicom_tag_header_t*>(
            next_sequence_element_pos);
    if (next_sequence_element_pos >= decoded_end ||
        next_sequence_element->element != 0xe000) {
      break;
    }
    ++cluster_count;
    pos = next_sequence_element_pos;
  }

  // preallocate memory for codeblocks and clusters
  if (image->data_chunks == NULL) {
    image->data_chunk_count = cluster_count;
    image->data_chunks = static_cast<isyntax_data_chunk_t*>(
        calloc(1, cluster_count * sizeof(isyntax_data_chunk_t)));
  }
  if (image->number_of_blocks <= 0) {
    return false;
  }
  if (image->codeblocks == NULL) {
    image->codeblock_count = image->number_of_blocks;
    image->codeblocks = static_cast<isyntax_codeblock_t*>(
        calloc(1, image->codeblock_count * sizeof(isyntax_codeblock_t)));
  }

  // pass 2: fill in all the information for each cluster
  pos = block_header_start;
  int32_t running_codeblock_index = 0;
  for (int32_t i = 0; i < cluster_count; ++i) {
    sequence_element = *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);
    uint8_t* next_sequence_element_pos =
        pos + sizeof(isyntax_dicom_tag_header_t) +
        reinterpret_cast<isyntax_dicom_tag_header_t*>(pos)->size;

    uint32_t cluster_block_size = sequence_element.size;
    uint8_t* cluster_block_end =
        pos + sizeof(isyntax_dicom_tag_header_t) + cluster_block_size;
    if (cluster_block_end > decoded_end) {
      return false;
    }

    // advance to cluster coordinates
    pos += sizeof(isyntax_dicom_tag_header_t);
    isyntax_dicom_tag_header_t element =
        *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);
    uint8_t* next_element =
        pos + sizeof(isyntax_dicom_tag_header_t) + element.size;
    if (next_element > cluster_block_end) {
      return false;
    }

    int32_t cluster_coordinate_count = element.size / 4;
    int32_t* coordinates =
        reinterpret_cast<int32_t*>(pos + sizeof(isyntax_dicom_tag_header_t));
    if (cluster_coordinate_count < 2) {
      return false;
    }
    int32_t cluster_x = coordinates[0];
    int32_t cluster_y = coordinates[1];

    // read cluster header template ID
    pos = next_element;
    element = *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);
    next_element = pos + sizeof(isyntax_dicom_tag_header_t) + element.size;
    if (next_element > cluster_block_end || element.size != 4) {
      return false;
    }
    uint32_t cluster_header_template_id =
        *reinterpret_cast<uint32_t*>(pos + sizeof(isyntax_dicom_tag_header_t));
    if (cluster_header_template_id >= isyntax->cluster_header_template_count) {
      return false;
    }
    isyntax_cluster_header_template_t* cluster_header_template =
        isyntax->cluster_header_templates + cluster_header_template_id;
    if (cluster_coordinate_count >= 3) {
      ASSERT(cluster_header_template->base_scale == coordinates[2]);
    }

    // read cluster data offset
    pos = next_element;
    element = *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);
    next_element = pos + sizeof(isyntax_dicom_tag_header_t) + element.size;
    if (next_element > cluster_block_end || element.size != 8) {
      return false;
    }
    uint64_t cluster_data_offset =
        *reinterpret_cast<uint64_t*>(pos + sizeof(isyntax_dicom_tag_header_t));

    // read cluster size
    pos = next_element;
    element = *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);
    next_element = pos + sizeof(isyntax_dicom_tag_header_t) + element.size;
    if (next_element > cluster_block_end || element.size != 8) {
      return false;
    }
    uint64_t cluster_size =
        *reinterpret_cast<uint64_t*>(pos + sizeof(isyntax_dicom_tag_header_t));

    // read cluster block data offsets
    pos = next_element;
    element = *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);
    next_element = pos + sizeof(isyntax_dicom_tag_header_t) + element.size;
    if (next_element > cluster_block_end) {
      return false;
    }
    uint32_t block_count = element.size / 4;
    uint32_t* cluster_block_data_offsets =
        reinterpret_cast<uint32_t*>(pos + sizeof(isyntax_dicom_tag_header_t));

    // read cluster block sizes
    pos = next_element;
    element = *reinterpret_cast<isyntax_dicom_tag_header_t*>(pos);
    next_element = pos + sizeof(isyntax_dicom_tag_header_t) + element.size;
    if (next_element > cluster_block_end || element.size / 4 != block_count) {
      return false;
    }
    uint32_t* cluster_block_sizes =
        reinterpret_cast<uint32_t*>(pos + sizeof(isyntax_dicom_tag_header_t));

    int32_t top_codeblock_index = running_codeblock_index;
    [[maybe_unused]] bool has_ll = false;
    int32_t highest_scale = 0;
    ASSERT(running_codeblock_index + static_cast<int32_t>(block_count) <=
           image->codeblock_count);
    for (int32_t j = 0; j < static_cast<int32_t>(block_count); ++j) {
      isyntax_codeblock_t* codeblock =
          image->codeblocks + running_codeblock_index;
      isyntax_cluster_relative_coords_t* relative_codeblock_in_cluster_info =
          cluster_header_template->relative_coords_for_codeblock_in_cluster + j;
      codeblock->x_coordinate =
          cluster_x + relative_codeblock_in_cluster_info->x;
      codeblock->y_coordinate =
          cluster_y + relative_codeblock_in_cluster_info->y;
      codeblock->color_component =
          relative_codeblock_in_cluster_info->color_component;
      codeblock->scale = relative_codeblock_in_cluster_info->scale;
      if (static_cast<int32_t>(codeblock->scale) > highest_scale) {
        highest_scale = static_cast<int32_t>(codeblock->scale);
      }
      codeblock->coefficient =
          (relative_codeblock_in_cluster_info->waveletcoeff == 3) ? 0 : 1;
      if (codeblock->coefficient == 0) {
        has_ll = true;
      }
      codeblock->block_data_offset =
          cluster_data_offset + cluster_block_data_offsets[j];
      codeblock->block_size = cluster_block_sizes[j];
      codeblock->block_header_template_id =
          relative_codeblock_in_cluster_info->block_header_template_id;
      ++running_codeblock_index;
    }

    isyntax_data_chunk_t* cluster = image->data_chunks + i;
    cluster->offset = cluster_data_offset + cluster_block_data_offsets[0];
    cluster->size = static_cast<uint32_t>(cluster_size);
    cluster->top_codeblock_index = top_codeblock_index;
    cluster->codeblock_count_per_color = static_cast<int32_t>(block_count / 3);
    cluster->scale = highest_scale;
    ASSERT(cluster->codeblock_count_per_color ==
           isyntax::chunk::GetChunkCodeblocksPerColorForLevel(highest_scale,
                                                              has_ll));

    pos = next_sequence_element_pos;
  }

  // Release excess allocated memory.
  if (running_codeblock_index < image->codeblock_count) {
    image->codeblock_count = running_codeblock_index;
    isyntax_codeblock_t* shrunk = static_cast<isyntax_codeblock_t*>(
        realloc(image->codeblocks,
                image->codeblock_count * sizeof(isyntax_codeblock_t)));
    ASSERT(shrunk);
    image->codeblocks = shrunk;
  }
  return true;
}

void ParseDimensionRange(isyntax_t* isyntax, isyntax_image_t* image,
                         char* value) {
  isyntax_image_dimension_range_t range = {0};
  isyntax::math::ParseThreeIntegers(value, &range.start, &range.step,
                                    &range.end);
  int32_t step_nonzero = (range.step != 0) ? range.step : 1;
  range.numsteps = ((range.end + range.step) - range.start) / step_nonzero;

  if (isyntax->parser.data_object_flags &
      ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate) {
    isyntax_block_header_template_t* tmpl =
        isyntax->block_header_templates +
        isyntax->parser.block_header_template_index;
    switch (isyntax->parser.dimension_index) {
      default:
        break;
      case 0:
        tmpl->block_width = range.numsteps;
        break;
      case 1:
        tmpl->block_height = range.numsteps;
        break;
      case 2:
        tmpl->color_component = range.start;
        break;
      case 3:
        tmpl->scale = range.start;
        break;
      case 4:
        tmpl->waveletcoeff = (range.start == 0) ? 1 : 3;
        break;
    }
  } else if (isyntax->parser.data_object_flags &
             ISYNTAX_OBJECT_UFSImageGeneralHeader) {
    switch (isyntax->parser.dimension_index) {
      default:
        break;
      case 0: {
        image->offset_x = range.start;
        image->width_including_padding = range.numsteps;
      } break;
      case 1: {
        image->offset_y = range.start;
        image->height_including_padding = range.numsteps;
      } break;
      case 2:
        break;  // always 3 color channels
      case 3: {
        image->level_count = range.numsteps;
        image->max_scale = range.numsteps - 1;
        image->level0_padding =
            (kPerLevelPadding << range.numsteps) - kPerLevelPadding;
        image->width =
            image->width_including_padding - 2 * image->level0_padding;
        image->height =
            image->height_including_padding - 2 * image->level0_padding;
      } break;
      case 4:
        break;  // always 4 wavelet coefficients
    }
  } else if (isyntax->parser.data_object_flags &
             ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate) {
    isyntax_cluster_header_template_t* tmpl =
        isyntax->cluster_header_templates +
        isyntax->parser.cluster_header_template_index;
    switch (isyntax->parser.dimension_index) {
      default:
        break;
      case 0:
        tmpl->base_x = range.start;
        break;
      case 1:
        tmpl->base_y = range.start;
        break;
      case 2:
        tmpl->base_color_component = range.start;
        break;
      case 3:
        tmpl->base_scale = range.start;
        break;
      case 4:
        tmpl->base_waveletcoeff = range.start;
        break;
    }
  }
}

aifocore::Status ParseGroup301D(isyntax_t* isyntax, isyntax_image_t* image,
                                uint32_t element, char* value,
                                uint64_t value_len) {
  bool success = true;

  switch (element) {
    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format(
              "Unknown element (group=0x301D, element=0x{:04x})", element));
    case 0x1001: /*PIM_DP_UFS_INTERFACE_VERSION*/ {
      // Present in the header; not used by fastslide here.
      static_cast<void>(value);
      static_cast<void>(value_len);
    } break;
    case 0x1002: /*PIM_DP_UFS_BARCODE*/ {
      // Present in the header; handled in UFSImport.
      static_cast<void>(value);
      static_cast<void>(value_len);
    } break;
    case 0x1003: /*PIM_DP_SCANNED_IMAGES*/ {
      // Container marker.
      static_cast<void>(value);
      static_cast<void>(value_len);
    } break;
    case 0x1010: /*PIM_DP_SCANNER_RACK_PRIORITY*/ {
      // Not used by fastslide; ignore.
      static_cast<void>(isyntax);
      static_cast<void>(image);
      static_cast<void>(value);
      static_cast<void>(value_len);
    } break;
    case 0x1013: /*DP_COLOR_MANAGEMENT*/ {
    } break;
    case 0x1014: /*DP_IMAGE_POST_PROCESSING*/ {
    } break;
    case 0x1015: /*DP_SHARPNESS_GAIN_RGB24*/ {
    } break;
    case 0x1016: /*DP_CLAHE_CLIP_LIMIT_Y16*/ {
    } break;
    case 0x1017: /*DP_CLAHE_NR_BINS_Y16*/ {
    } break;
    case 0x1018: /*DP_CLAHE_CONTEXT_DIMENSION_Y16*/ {
    } break;
    case 0x1019: /*DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR*/ {
    } break;
    case 0x101A: /*DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL*/ {
    } break;
    case 0x101B: /*DP_WAVELET_QUANTIZER*/ {
    } break;
    case 0x101C: /*DP_WAVELET_DEADZONE*/ {
    } break;
    case 0x1025: /*UFS_IMAGE_OPP_EXTREME_VERTEX*/ {  // data model >= 100
    } break;
    case 0x1004: /*PIM_DP_IMAGE_TYPE*/ {
      if ((strcmp(value, "MACROIMAGE") == 0)) {
        isyntax->macro_image_index = isyntax->parser.running_image_index;
        isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
        image->image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
      } else if ((strcmp(value, "LABELIMAGE") == 0)) {
        isyntax->label_image_index = isyntax->parser.running_image_index;
        isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
        image->image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
      } else if ((strcmp(value, "WSI") == 0)) {
        isyntax->wsi_image_index = isyntax->parser.running_image_index;
        isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_WSI;
        image->image_type = ISYNTAX_IMAGE_TYPE_WSI;
      }
    } break;
    case 0x1005: { /*PIM_DP_IMAGE_DATA*/
      if (value_len > 0) {
        char last_char = '\0';
        if (isyntax->parser.content_was_skipped &&
            isyntax->parser.content_last_char_valid) {
          last_char = isyntax->parser.content_last_char;
        } else {
          last_char = value[value_len - 1];
        }
        if (last_char == '/') {
          value_len--;
        }
      }
      image->base64_encoded_jpg_file_offset =
          isyntax->parser.content_file_offset;
      image->base64_encoded_jpg_len = value_len;
    } break;
    case 0x2001: /*UFS_IMAGE_NUMBER_OF_BLOCKS*/ {
      image->number_of_blocks = atoi(value);
    } break;
    case 0x2000: /*UFS_IMAGE_GENERAL_HEADERS*/ {
    } break;
    case 0x2002: /*UFS_IMAGE_DIMENSIONS_OVER_BLOCK*/ {
    } break;
    case 0x2003: /*UFS_IMAGE_DIMENSIONS*/ {
    } break;
    case 0x2004: /*UFS_IMAGE_DIMENSION_NAME*/ {
    } break;
    case 0x2005: /*UFS_IMAGE_DIMENSION_TYPE*/ {
    } break;
    case 0x2006: /*UFS_IMAGE_DIMENSION_UNIT*/ {
    } break;
    case 0x2007: /*UFS_IMAGE_DIMENSION_SCALE_FACTOR*/ {
      float mpp = atof(value);
      if (isyntax->parser.dimension_index == 0 /*x*/) {
        isyntax->mpp_x = mpp;
        isyntax->is_mpp_known = true;
      } else if (isyntax->parser.dimension_index == 1 /*y*/) {
        isyntax->mpp_y = mpp;
        isyntax->is_mpp_known = true;
      }
    } break;
    case 0x2008: /*UFS_IMAGE_DIMENSION_DISCRETE_VALUES_STRING*/ {
    } break;
    case 0x2009: /*UFS_IMAGE_BLOCK_HEADER_TEMPLATES*/ {
    } break;
    case 0x200A: /*UFS_IMAGE_DIMENSION_RANGES*/ {
    } break;
    case 0x200B: /*UFS_IMAGE_DIMENSION_RANGE*/ {
      ParseDimensionRange(isyntax, image, value);
    } break;
    case 0x200C: /*UFS_IMAGE_DIMENSION_IN_BLOCK*/ {
    } break;
    case 0x200D: /*UFS_IMAGE_BLOCK_HEADERS*/ {  // data model >= 100
    } break;
    case 0x200E: /*UFS_IMAGE_BLOCK_COORDINATE*/ {
      if (isyntax->parser.data_object_flags &
          (ISYNTAX_OBJECT_UFSImageBlockHeader |
           ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate)) {
        isyntax_cluster_header_template_t* tmpl =
            isyntax->cluster_header_templates +
            isyntax->parser.cluster_header_template_index;
        isyntax::math::ParseUpToFiveIntegers(
            value, reinterpret_cast<int32_t*>(
                       tmpl->relative_coords_for_codeblock_in_cluster
                           [isyntax->parser.block_header_index_for_cluster]
                               .raw_coords));
      }
    } break;
    case 0x200F: /*UFS_IMAGE_BLOCK_COMPRESSION_METHOD*/ {
      int32_t compression_method = atoi(value);
      if (compression_method == 16) {
        image->compressor_version = 1;
      } else if (compression_method == 19) {
        image->compressor_version = 2;
      } else {
        success = false;
      }
    } break;
    case 0x2012: /*UFS_IMAGE_BLOCK_HEADER_TEMPLATE_ID*/ {
      if (isyntax->parser.data_object_flags &
          (ISYNTAX_OBJECT_UFSImageBlockHeader |
           ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate)) {
        isyntax_cluster_header_template_t* tmpl =
            isyntax->cluster_header_templates +
            isyntax->parser.cluster_header_template_index;
        isyntax_cluster_relative_coords_t*
            relative_coords_for_codeblock_in_cluster =
                tmpl->relative_coords_for_codeblock_in_cluster +
                isyntax->parser.block_header_index_for_cluster;
        relative_coords_for_codeblock_in_cluster->block_header_template_id =
            atoi(value);
      }
    } break;
    case 0x2013: /*UFS_IMAGE_PIXEL_TRANSFORMATION_METHOD*/ {
      // Present in the header; not required by fastslide.
      static_cast<void>(value);
      static_cast<void>(value_len);
    } break;
    case 0x2014: { /*UFS_IMAGE_BLOCK_HEADER_TABLE*/
      success = ParseBlockHeaderTable(image, value, value_len);
    } break;
    case 0x2016: /*UFS_IMAGE_CLUSTER_HEADER_TEMPLATES*/ {  // data model >= 100
    } break;
    case 0x2017: /*UFS_IMAGE_DIMENSIONS_OVER_CLUSTER*/ {  // data model >= 100
    } break;
    case 0x201F: /*UFS_IMAGE_CLUSTER_HEADER_TABLE*/ {
      success = ParseClusterHeaderTable(isyntax, image, value, value_len);
    } break;
    case 0x2021: /*UFS_IMAGE_DIMENSIONS_IN_CLUSTER*/ {
      isyntax_cluster_header_template_t* tmpl =
          isyntax->cluster_header_templates +
          isyntax->parser.cluster_header_template_index;
      tmpl->dimension_count =
          isyntax::math::ParseUpToFiveIntegers(value, tmpl->dimension_order);
    } break;
    case 0x2023: /*UFS_IMAGE_VALID_DATA_ENVELOPES*/ {  // data model >= 100
    } break;
    case 0x2024: /*UFS_IMAGE_OPP_EXTREME_VERTICES*/ {  // data model >= 100
    } break;
    case 0x2025: /*UFS_IMAGE_OPP_EXTREME_VERTEX*/ {
      if (isyntax->parser.data_object_flags &
          (ISYNTAX_OBJECT_UFSImageValidDataEnvelope)) {
        isyntax_valid_data_envelope_t* envelope =
            isyntax->valid_data_envelopes +
            isyntax->parser.valid_data_envelope_index;
        if (envelope->vertex_count < COUNT(envelope->vertices)) {
          v2i vertex = {};
          isyntax::math::AtoiAndAdvance(
              isyntax::math::AtoiAndAdvance(value, &vertex.x), &vertex.y);
          envelope->vertices[envelope->vertex_count++] = vertex;
        } else {
          success = false;
        }
      }
    } break;
    case 0x2026: /*UFS_IMAGE_VALID_ENVELOPE_DIMENSIONS*/ {  // data model >= 100
    } break;
    case 0x2027: /*UFS_IMAGE_DIMENSION_ORIGIN*/ {  // data model >= 100
    } break;
    case 0x2029: /*UFS_IMAGE_PIXEL_TRANSFORM_METHOD*/ {  // data model >= 100
    } break;
  }

  return success ? aifocore::Status::OkStatus()
                 : AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kDataLoss,
                                        "ParseGroup301D failed");
}

}  // namespace

namespace isyntax::xml {

aifocore::Status ParseUfsimportChildNode(isyntax_t* isyntax, uint32_t group,
                                         uint32_t element, char* value,
                                         uint64_t value_len) {
  static_cast<void>(value_len);
  switch (group) {
    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format("Unknown group (0x{:04x})", group));
    case 0x0008: {
      switch (element) {
        default:
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kUnimplemented,
              aifocore::fmt::format(
                  "Unknown element (group=0x{:04x}, element=0x{:04x})", group,
                  element));
        case 0x002A: /*DICOM_ACQUISITION_DATETIME*/ {
        } break;
        case 0x0070: /*DICOM_MANUFACTURER*/ {
        } break;
        case 0x1090: /*DICOM_MANUFACTURERS_MODEL_NAME*/ {
        } break;
      }
    } break;
    case 0x0018: {
      switch (element) {
        default:
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kUnimplemented,
              aifocore::fmt::format(
                  "Unknown element (group=0x{:04x}, element=0x{:04x})", group,
                  element));
        case 0x1000: /*DICOM_DEVICE_SERIAL_NUMBER*/ {
        } break;
        case 0x1020: /*DICOM_SOFTWARE_VERSIONS*/ {
        } break;
        case 0x1200: /*DICOM_DATE_OF_LAST_CALIBRATION*/ {
        } break;
        case 0x1201: /*DICOM_TIME_OF_LAST_CALIBRATION*/ {
        } break;
      }
    } break;
    case 0x101D: {
      switch (element) {
        default:
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kUnimplemented,
              aifocore::fmt::format(
                  "Unknown element (group=0x{:04x}, element=0x{:04x})", group,
                  element));
        case 0x1007: /*PIIM_DP_SCANNER_RACK_NUMBER*/ {
        } break;
        case 0x1008: /*PIIM_DP_SCANNER_SLOT_NUMBER*/ {
        } break;
        case 0x1009: /*PIIM_DP_SCANNER_OPERATOR_ID*/ {
        } break;
        case 0x100A: /*PIIM_DP_SCANNER_CALIBRATION_STATUS*/ {
        } break;
      }
    } break;
    case 0x301D: {
      switch (element) {
        default:
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kUnimplemented,
              aifocore::fmt::format(
                  "Unknown element (group=0x{:04x}, element=0x{:04x})", group,
                  element));
        case 0x1003: /*PIM_DP_SCANNED_IMAGES*/ {
          // Container marker; ignore.
          static_cast<void>(value);
          static_cast<void>(value_len);
        } break;
        case 0x1010: /*PIM_DP_SCANNER_RACK_PRIORITY*/ {
          // Not used; ignore.
          static_cast<void>(value);
          static_cast<void>(value_len);
        } break;
        case 0x1001: /*PIM_DP_UFS_INTERFACE_VERSION*/ {
          isyntax->data_model_major_version = atoi(value);
        } break;
        case 0x1002: /*PIM_DP_UFS_BARCODE*/ {
          if (value_len > 0) {
            auto decoded_res = isyntax::Base64Decode(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(value), value_len));
            if (decoded_res.ok()) {
              const std::vector<uint8_t>& decoded = decoded_res.value();
              const size_t copy_len =
                  MIN(decoded.size(), sizeof(isyntax->barcode) - 1);
              if (copy_len > 0) {
                std::memcpy(isyntax->barcode, decoded.data(), copy_len);
              }
            }
          }
          isyntax->is_barcode_read = true;
        } break;
      }
    } break;
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status ParseScannedimageChildNode(isyntax_t* isyntax, uint32_t group,
                                            uint32_t element, char* value,
                                            uint64_t value_len) {
  isyntax_image_t* image = isyntax->parser.current_image;
  if (!image) {
    image = isyntax->parser.current_image = &isyntax->images[0];
  }

  switch (group) {
    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format("Unknown group (0x{:04x})", group));
    case 0x0008: {
      return ParseGroup0008(element, value, value_len);
    }
    case 0x0028: {
      return ParseGroup0028(isyntax, image, element, value, value_len);
    }
    case 0x301D: {
      return ParseGroup301D(isyntax, image, element, value, value_len);
    }
  }
}

}  // namespace isyntax::xml
