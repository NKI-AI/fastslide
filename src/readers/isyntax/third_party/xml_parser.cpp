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

#include "fastslide/readers/isyntax/third_party/xml_parser.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/third_party/platform/intrinsics.h"
#include "fastslide/readers/isyntax/third_party/third_party/yxml.h"
#include "fastslide/readers/isyntax/third_party/xml_semantics.h"

constexpr size_t kYxmlStackBytes = KILOBYTES(32);
constexpr size_t kInitialAttrBytes = KILOBYTES(32);
constexpr size_t kInitialContentBytes = MEGABYTES(8);

struct YxmlArena {
  std::unique_ptr<std::uint8_t[]> storage;
  yxml_t* x = nullptr;

  YxmlArena() {
    storage =
        std::make_unique<std::uint8_t[]>(sizeof(yxml_t) + kYxmlStackBytes);
    x = reinterpret_cast<yxml_t*>(storage.get());
    void* stack = storage.get() + sizeof(yxml_t);
    yxml_init(x, stack, kYxmlStackBytes);
  }
};

// Definition for the forward-declared C type `isyntax_xml_cpp_state_t` in
// `isyntax.h`. This is owned by `isyntax_t::xml_cpp_state`.
struct isyntax_xml_cpp_state_t {
  YxmlArena yxml;
  std::vector<char> attrbuf;
  std::vector<char> contentbuf;
  aifocore::Status last_error = aifocore::Status::OkStatus();

  isyntax_xml_cpp_state_t()
      : attrbuf(kInitialAttrBytes), contentbuf(kInitialContentBytes) {
    attrbuf[0] = '\0';
    contentbuf[0] = '\0';
  }
};

namespace {

void SetXmlErrorOnce(isyntax_t* isyntax, aifocore::Status st) {
  if (isyntax == nullptr) {
    return;
  }
  if (isyntax->xml_cpp_state == nullptr) {
    return;
  }
  if (!isyntax->xml_cpp_state->last_error.ok()) {
    return;
  }
  isyntax->xml_cpp_state->last_error = std::move(st);
}

enum class ContentPolicy {
  kBufferViaYxml,
  kBufferAndSkipFast,
  kSkipBufferAndCountFast,
};

static ContentPolicy GetContentPolicy(uint32_t group, uint32_t element) {
  // Large base64 blobs where we only need file_offset + length (+ trailing
  // '/').
  if ((group == 0x301D && element == 0x1005) ||
      (group == 0x0028 && element == 0x2000)) {
    return ContentPolicy::kSkipBufferAndCountFast;
  }
  // Large text that we must parse, but we still want to skip fast.
  if (group == 0x301D && element == 0x2014) {  // UFS_IMAGE_BLOCK_HEADER_TABLE
    return ContentPolicy::kBufferAndSkipFast;
  }
  return ContentPolicy::kBufferViaYxml;
}

static const char* GetSpaces(int32_t length) {
  ASSERT(length >= 0);
  static constexpr char kSpaces[] = "                                  ";
  int32_t spaces_len = static_cast<int32_t>(sizeof(kSpaces) - 1);
  int32_t offset_from_end = MIN(spaces_len, length);
  int32_t offset = spaces_len - offset_from_end;
  return kSpaces + offset;
}

static aifocore::Status ValidateDicomAttr(const char* observed,
                                          const char* expected) {
  if (std::strcmp(observed, expected) == 0) {
    return aifocore::Status::OkStatus();
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kDataLoss,
      aifocore::fmt::format(
          "iSyntax validation error: expected '{}' but found '{}'", expected,
          observed));
}

static void SyncParserPointersFromState(isyntax_xml_parser_t* parser,
                                        isyntax_xml_cpp_state_t* state) {
  parser->x = state->yxml.x;

  parser->attrbuf = state->attrbuf.data();
  parser->attrbuf_capacity = state->attrbuf.size();
  parser->attrbuf_end = parser->attrbuf + parser->attrbuf_capacity;

  parser->contentbuf = state->contentbuf.data();
  parser->contentbuf_capacity = state->contentbuf.size();
}

static void GrowAttrBuffer(isyntax_xml_parser_t* parser,
                           isyntax_xml_cpp_state_t* state,
                           size_t new_capacity) {
  ptrdiff_t cur_offset =
      (parser->attrcur != nullptr) ? (parser->attrcur - parser->attrbuf) : 0;
  state->attrbuf.resize(new_capacity);
  parser->attrbuf = state->attrbuf.data();
  parser->attrbuf_capacity = state->attrbuf.size();
  parser->attrbuf_end = parser->attrbuf + parser->attrbuf_capacity;
  parser->attrcur =
      (parser->attrcur != nullptr) ? (parser->attrbuf + cur_offset) : nullptr;
}

static void GrowContentBuffer(isyntax_xml_parser_t* parser,
                              isyntax_xml_cpp_state_t* state,
                              size_t new_capacity) {
  ptrdiff_t cur_offset = (parser->contentcur != nullptr)
                             ? (parser->contentcur - parser->contentbuf)
                             : 0;
  state->contentbuf.resize(new_capacity);
  parser->contentbuf = state->contentbuf.data();
  parser->contentbuf_capacity = state->contentbuf.size();
  parser->contentcur = (parser->contentcur != nullptr)
                           ? (parser->contentbuf + cur_offset)
                           : nullptr;
}

static bool EnsureAttrCapacity(isyntax_xml_parser_t* parser,
                               isyntax_xml_cpp_state_t* state, size_t needed) {
  if (needed <= parser->attrbuf_capacity) {
    return true;
  }
  size_t new_capacity = parser->attrbuf_capacity;
  while (new_capacity < needed) {
    new_capacity *= 2;
  }
  GrowAttrBuffer(parser, state, new_capacity);
  return true;
}

static bool EnsureContentCapacity(isyntax_xml_parser_t* parser,
                                  isyntax_xml_cpp_state_t* state,
                                  size_t needed) {
  if (needed <= parser->contentbuf_capacity) {
    return true;
  }
  size_t new_capacity = parser->contentbuf_capacity;
  while (new_capacity < needed) {
    new_capacity *= 2;
  }
  GrowContentBuffer(parser, state, new_capacity);
  return true;
}

static bool SetDataObjectFlags(isyntax_t* isyntax, uint32_t* flags,
                               uint32_t parent_element) {
  switch (parent_element) {
    default:
      break;
    case 0:
      *flags |= ISYNTAX_OBJECT_DPUfsImport;
      break;
    case PIM_DP_SCANNED_IMAGES:
      *flags |= ISYNTAX_OBJECT_DPScannedImage;
      break;
    case UFS_IMAGE_GENERAL_HEADERS:
      *flags |= ISYNTAX_OBJECT_UFSImageGeneralHeader;
      break;
    case UFS_IMAGE_BLOCK_HEADER_TEMPLATES:
      *flags |= ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate;
      if (isyntax->parser.block_header_template_index >=
          COUNT(isyntax->block_header_templates)) {
        return false;
      }
      break;
    case UFS_IMAGE_DIMENSIONS:
      *flags |= ISYNTAX_OBJECT_UFSImageDimension;
      break;
    case UFS_IMAGE_DIMENSION_RANGES:
      *flags |= ISYNTAX_OBJECT_UFSImageDimensionRange;
      break;
    case DP_COLOR_MANAGEMENT:
      *flags |= ISYNTAX_OBJECT_DPColorManagement;
      break;
    case DP_IMAGE_POST_PROCESSING:
      *flags |= ISYNTAX_OBJECT_DPImagePostProcessing;
      break;
    case DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR:
      *flags |= ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor;
      break;
    case DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL:
      *flags |= ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel;
      break;
    case PIIM_PIXEL_DATA_REPRESENTATION_SEQUENCE:
      *flags |= ISYNTAX_OBJECT_PixelDataRepresentation;
      break;
    case UFS_IMAGE_BLOCK_HEADERS:
      *flags |= ISYNTAX_OBJECT_UFSImageBlockHeader;
      break;
    case UFS_IMAGE_CLUSTER_HEADER_TEMPLATES:
      *flags |= ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate;
      if (isyntax->parser.cluster_header_template_index >=
          COUNT(isyntax->cluster_header_templates)) {
        return false;
      }
      break;
    case UFS_IMAGE_VALID_DATA_ENVELOPES:
      if (isyntax->parser.valid_data_envelope_index <
          COUNT(isyntax->valid_data_envelopes)) {
        *flags |= ISYNTAX_OBJECT_UFSImageValidDataEnvelope;
      } else {
        SetXmlErrorOnce(
            isyntax,
            AIFOCORE_MAKE_STATUS(
                aifocore::StatusCode::kResourceExhausted,
                aifocore::fmt::format(
                    "UFSImageValidDataEnvelopes out of bounds (index={} "
                    "capacity={})",
                    isyntax->parser.valid_data_envelope_index,
                    COUNT(isyntax->valid_data_envelopes))));
        return false;
      }
      break;
    case UFS_IMAGE_OPP_EXTREME_VERTICES:
      *flags |= ISYNTAX_OBJECT_UFSImageOppExtremeVertex;
      break;
  }
  return true;
}

static bool ResetDataObjectFlags(isyntax_xml_parser_t* parser, uint32_t* flags,
                                 uint32_t data_object_element) {
  switch (data_object_element) {
    default:
      break;
    case 0:
      *flags &= ~ISYNTAX_OBJECT_DPUfsImport;
      break;
    case PIM_DP_SCANNED_IMAGES:
      *flags &= ~ISYNTAX_OBJECT_DPScannedImage;
      break;
    case UFS_IMAGE_GENERAL_HEADERS:
      *flags &= ~ISYNTAX_OBJECT_UFSImageGeneralHeader;
      parser->dimension_index = 0;
      break;
    case UFS_IMAGE_BLOCK_HEADER_TEMPLATES:
      *flags &= ~ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate;
      ++parser->block_header_template_index;
      parser->dimension_index = 0;
      break;
    case UFS_IMAGE_DIMENSIONS:
      *flags &= ~ISYNTAX_OBJECT_UFSImageDimension;
      ++parser->dimension_index;
      break;
    case UFS_IMAGE_DIMENSION_RANGES:
      *flags &= ~ISYNTAX_OBJECT_UFSImageDimensionRange;
      ++parser->dimension_index;
      break;
    case DP_COLOR_MANAGEMENT:
      *flags &= ~ISYNTAX_OBJECT_DPColorManagement;
      break;
    case DP_IMAGE_POST_PROCESSING:
      *flags &= ~ISYNTAX_OBJECT_DPImagePostProcessing;
      break;
    case DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR:
      *flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor;
      break;
    case DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL:
      *flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel;
      break;
    case UFS_IMAGE_BLOCK_HEADERS:
      *flags &= ~ISYNTAX_OBJECT_UFSImageBlockHeader;
      if (*flags & ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate) {
        ++parser->block_header_index_for_cluster;
        if (parser->block_header_index_for_cluster >=
            MAX_CODEBLOCKS_PER_CLUSTER) {
          return false;
        }
      }
      break;
    case UFS_IMAGE_CLUSTER_HEADER_TEMPLATES:
      *flags &= ~ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate;
      parser->dimension_index = 0;
      parser->block_header_index_for_cluster = 0;
      break;
    case UFS_IMAGE_VALID_DATA_ENVELOPES:
      *flags &= ~ISYNTAX_OBJECT_UFSImageValidDataEnvelope;
      ++parser->valid_data_envelope_index;
      break;
    case UFS_IMAGE_OPP_EXTREME_VERTICES:
      *flags &= ~ISYNTAX_OBJECT_UFSImageOppExtremeVertex;
      break;
  }
  return true;
}

static void FinalizeClusterHeaderTemplate(isyntax_t* isyntax,
                                          int32_t template_index) {
  isyntax_cluster_header_template_t* templ =
      isyntax->cluster_header_templates + template_index;
  templ->codeblock_in_cluster_count =
      isyntax->parser.block_header_index_for_cluster;
  for (int32_t i = 0; i < templ->codeblock_in_cluster_count; ++i) {
    isyntax_cluster_relative_coords_t* relative =
        templ->relative_coords_for_codeblock_in_cluster + i;
    relative->x = templ->base_x;
    relative->y = templ->base_y;
    relative->color_component = templ->base_color_component;
    relative->scale = templ->base_scale;
    relative->waveletcoeff = templ->base_waveletcoeff;
    uint32_t* dimensions_to_fix[5] = {
        &relative->x, &relative->y, &relative->color_component,
        &relative->scale, &relative->waveletcoeff};
    for (int32_t dimension_index = 0; dimension_index < templ->dimension_count;
         ++dimension_index) {
      uint32_t* to_fix =
          dimensions_to_fix[templ->dimension_order[dimension_index]];
      *to_fix += relative->raw_coords[dimension_index];
    }
  }
}

static bool HandleElemStart(isyntax_t* isyntax, yxml_t* x) {
  isyntax_xml_parser_t* parser = &isyntax->parser;

  if (parser->node_stack_index + 1 >= ISYNTAX_MAX_NODE_DEPTH) {
    SetXmlErrorOnce(isyntax, AIFOCORE_MAKE_STATUS(
                                 aifocore::StatusCode::kResourceExhausted,
                                 "iSyntax XML error: node stack overflow"));
    return false;
  }

  isyntax_parser_node_t* parent_node =
      parser->node_stack + parser->node_stack_index;
  ++parser->node_stack_index;
  isyntax_parser_node_t* node = parser->node_stack + parser->node_stack_index;
  std::memset(node, 0, sizeof(isyntax_parser_node_t));
  node->group = parent_node->group;
  node->element = parent_node->element;

  parser->contentcur = parser->contentbuf;
  parser->contentbuf[0] = '\0';
  parser->contentlen = 0;
  parser->content_file_offset = 0;
  parser->content_last_char = '\0';
  parser->content_last_char_valid = false;
  parser->content_was_skipped = false;
  parser->attribute_index = 0;

  if (std::strcmp(x->elem, "Attribute") == 0) {
    node->node_type = ISYNTAX_NODE_LEAF;
  } else if (std::strcmp(x->elem, "DataObject") == 0) {
    node->node_type = ISYNTAX_NODE_BRANCH;

    if (parser->data_object_stack_index + 1 >= ISYNTAX_MAX_NODE_DEPTH) {
      SetXmlErrorOnce(isyntax,
                      AIFOCORE_MAKE_STATUS(
                          aifocore::StatusCode::kResourceExhausted,
                          "iSyntax XML error: data object stack overflow"));
      return false;
    }

    ++parser->data_object_stack_index;
    parser->data_object_stack[parser->data_object_stack_index] = *parent_node;

    if (!SetDataObjectFlags(isyntax, &parser->data_object_flags,
                            parent_node->element)) {
      return false;
    }
  } else if (std::strcmp(x->elem, "Array") == 0) {
    node->node_type = ISYNTAX_NODE_ARRAY;
  } else {
    node->node_type = ISYNTAX_NODE_NONE;
  }

  parser->current_node_type = node->node_type;
  parser->current_node_has_children = false;
  return true;
}

static bool HandleContent(isyntax_t* isyntax, isyntax_xml_cpp_state_t* state,
                          yxml_t* x, int64_t chunk_offset,
                          const char* xml_header, const char* doc,
                          int64_t remaining_length, int64_t* bytes_to_skip) {
  *bytes_to_skip = 0;
  isyntax_xml_parser_t* parser = &isyntax->parser;

  if (parser->contentcur == nullptr) {
    return true;
  }

  if (parser->content_file_offset == 0) {
    parser->content_file_offset = chunk_offset + (doc - xml_header);
  }

  if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
    uint32_t group = parser->current_dicom_group_tag;
    uint32_t element = parser->current_dicom_element_tag;
    isyntax_parser_node_t* node = parser->node_stack + parser->node_stack_index;
    node->group = group;
    node->element = element;
    ContentPolicy policy = GetContentPolicy(group, element);
    if (policy != ContentPolicy::kBufferViaYxml) {
      parser->node_stack[parser->node_stack_index].has_base64_content = true;
      const char* content_start = doc;
      const void* pos = std::memchr(content_start, '<', remaining_length);
      if (pos != nullptr) {
        int64_t size = static_cast<const char*>(pos) - content_start;
        if (size > 0) {
          parser->content_last_char = content_start[size - 1];
          parser->content_last_char_valid = true;
        }
        parser->contentlen += static_cast<size_t>(size);
        if (policy == ContentPolicy::kSkipBufferAndCountFast) {
          parser->content_was_skipped = true;
        } else {
          size_t needed = parser->contentlen + 1;
          EnsureContentCapacity(parser, state, needed);
          std::memcpy(parser->contentbuf +
                          (parser->contentlen - static_cast<size_t>(size)),
                      content_start, static_cast<size_t>(size));
          parser->contentcur = parser->contentbuf + parser->contentlen;
          *parser->contentcur = '\0';
        }
        *bytes_to_skip = size;
        return true;
      }
      if (remaining_length > 0) {
        parser->content_last_char = content_start[remaining_length - 1];
        parser->content_last_char_valid = true;
      }
      parser->contentlen += static_cast<size_t>(remaining_length);
      if (policy == ContentPolicy::kSkipBufferAndCountFast) {
        parser->content_was_skipped = true;
      } else {
        size_t needed = parser->contentlen + 1;
        EnsureContentCapacity(parser, state, needed);
        std::memcpy(
            parser->contentbuf +
                (parser->contentlen - static_cast<size_t>(remaining_length)),
            content_start, static_cast<size_t>(remaining_length));
        parser->contentcur = parser->contentbuf + parser->contentlen;
        *parser->contentcur = '\0';
      }
      *bytes_to_skip = remaining_length;
      return true;
    }
  }

  size_t data_len = std::strlen(x->data);
  if (data_len > 0) {
    parser->content_last_char = x->data[data_len - 1];
    parser->content_last_char_valid = true;
  }
  size_t needed = parser->contentlen + data_len + 1;
  EnsureContentCapacity(parser, state, needed);
  std::memcpy(parser->contentbuf + parser->contentlen, x->data, data_len);
  parser->contentlen += data_len;
  parser->contentcur = parser->contentbuf + parser->contentlen;
  *parser->contentcur = '\0';
  return true;
}

static bool HandleElemEnd(isyntax_t* isyntax) {
  isyntax_xml_parser_t* parser = &isyntax->parser;

  if (parser->current_node_type == ISYNTAX_NODE_LEAF &&
      !parser->current_node_has_children) {
    if (parser->node_stack_index == 2) {
      aifocore::Status st = isyntax::xml::ParseUfsimportChildNode(
          isyntax, parser->current_dicom_group_tag,
          parser->current_dicom_element_tag, parser->contentbuf,
          parser->contentlen);
      if (!st.ok()) {
        SetXmlErrorOnce(isyntax, std::move(st));
        return false;
      }
    } else {
      aifocore::Status st = isyntax::xml::ParseScannedimageChildNode(
          isyntax, parser->current_dicom_group_tag,
          parser->current_dicom_element_tag, parser->contentbuf,
          parser->contentlen);
      if (!st.ok()) {
        SetXmlErrorOnce(isyntax, std::move(st));
        return false;
      }
    }
  } else {
    const char* elem_name = nullptr;
    if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
      elem_name = "Attribute";
    } else if (parser->current_node_type == ISYNTAX_NODE_BRANCH) {
      elem_name = "DataObject";
      if (parser->data_object_stack_index < 0) {
        SetXmlErrorOnce(isyntax,
                        AIFOCORE_MAKE_STATUS(
                            aifocore::StatusCode::kInternal,
                            "iSyntax XML error: data object stack underflow"));
        return false;
      }
      isyntax_parser_node_t data_object =
          parser->data_object_stack[parser->data_object_stack_index];
      --parser->data_object_stack_index;

      if (data_object.element == UFS_IMAGE_CLUSTER_HEADER_TEMPLATES) {
        FinalizeClusterHeaderTemplate(isyntax,
                                      parser->cluster_header_template_index);
        ++parser->cluster_header_template_index;
        if (isyntax->cluster_header_template_count <
            COUNT(isyntax->cluster_header_templates)) {
          ++isyntax->cluster_header_template_count;
        }
      } else if (data_object.element == UFS_IMAGE_BLOCK_HEADER_TEMPLATES) {
        if (isyntax->block_header_template_count <
            COUNT(isyntax->block_header_templates)) {
          ++isyntax->block_header_template_count;
        }
      } else if (data_object.element == UFS_IMAGE_VALID_DATA_ENVELOPES) {
        if (isyntax->valid_data_envelope_count <
            COUNT(isyntax->valid_data_envelopes)) {
          ++isyntax->valid_data_envelope_count;
        }
      }
      if (!ResetDataObjectFlags(parser, &parser->data_object_flags,
                                data_object.element)) {
        return false;
      }
    } else if (parser->current_node_type == ISYNTAX_NODE_ARRAY) {
      parser->dimension_index = 0;
      elem_name = "Array";
    }
  }

  if (parser->node_stack_index > 0) {
    --parser->node_stack_index;
    parser->current_node_type =
        parser->node_stack[parser->node_stack_index].node_type;
    parser->current_node_has_children =
        parser->node_stack[parser->node_stack_index].has_children;
  } else {
    SetXmlErrorOnce(
        isyntax,
        AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            "iSyntax XML error: closing element without matching start"));
    return false;
  }
  return true;
}

static bool HandleAttrVal(isyntax_xml_parser_t* parser,
                          isyntax_xml_cpp_state_t* state, yxml_t* x) {
  if (parser->attrcur == nullptr) {
    return true;
  }
  size_t data_len = std::strlen(x->data);
  size_t needed = parser->attrlen + data_len + 1;
  EnsureAttrCapacity(parser, state, needed);
  std::memcpy(parser->attrbuf + parser->attrlen, x->data, data_len);
  parser->attrlen += data_len;
  parser->attrcur = parser->attrbuf + parser->attrlen;
  *parser->attrcur = '\0';
  return true;
}

static bool HandleAttrEnd(isyntax_t* isyntax, yxml_t* x) {
  isyntax_xml_parser_t* parser = &isyntax->parser;
  bool paranoid_mode = isyntax->xml_paranoid_mode;

  if (parser->attrcur == nullptr) {
    return true;
  }
  ASSERT(std::strlen(parser->attrbuf) == parser->attrlen);

  if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
    if (parser->attribute_index == 0) {
      if (paranoid_mode) {
        aifocore::Status st = ValidateDicomAttr(x->attr, "Name");
        if (!st.ok()) {
          SetXmlErrorOnce(isyntax, std::move(st));
          return false;
        }
      }
      size_t copy_size =
          MIN(parser->attrlen, sizeof(parser->current_dicom_attribute_name));
      std::memcpy(parser->current_dicom_attribute_name, parser->attrbuf,
                  copy_size);
      int32_t one_past_last_char = MIN(
          parser->attrlen, sizeof(parser->current_dicom_attribute_name) - 1);
      parser->current_dicom_attribute_name[one_past_last_char] = '\0';
    } else if (parser->attribute_index == 1) {
      if (paranoid_mode) {
        aifocore::Status st = ValidateDicomAttr(x->attr, "Group");
        if (!st.ok()) {
          SetXmlErrorOnce(isyntax, std::move(st));
          return false;
        }
      }
      parser->current_dicom_group_tag =
          std::strtoul(parser->attrbuf, nullptr, 0);
    } else if (parser->attribute_index == 2) {
      if (paranoid_mode) {
        aifocore::Status st = ValidateDicomAttr(x->attr, "Element");
        if (!st.ok()) {
          SetXmlErrorOnce(isyntax, std::move(st));
          return false;
        }
      }
      parser->current_dicom_element_tag =
          std::strtoul(parser->attrbuf, nullptr, 0);
    } else if (parser->attribute_index == 3) {
      if (paranoid_mode)
        ValidateDicomAttr(x->attr, "PMSVR");
      if (std::strcmp(parser->attrbuf, "IDataObjectArray") == 0) {
        parser->current_node_has_children = true;
        parser->node_stack[parser->node_stack_index].has_children = true;
        if (parser->node_stack_index == 2) {
          aifocore::Status st = isyntax::xml::ParseUfsimportChildNode(
              isyntax, parser->current_dicom_group_tag,
              parser->current_dicom_element_tag, parser->contentbuf,
              parser->contentlen);
          if (!st.ok()) {
            SetXmlErrorOnce(isyntax, std::move(st));
            return false;
          }
        } else {
          aifocore::Status st = isyntax::xml::ParseScannedimageChildNode(
              isyntax, parser->current_dicom_group_tag,
              parser->current_dicom_element_tag, parser->contentbuf,
              parser->contentlen);
          if (!st.ok()) {
            SetXmlErrorOnce(isyntax, std::move(st));
            return false;
          }
        }
      }
    }
  } else if (parser->current_node_type == ISYNTAX_NODE_BRANCH) {
    ASSERT(parser->attribute_index == 0);
    ASSERT(std::strcmp(x->attr, "ObjectType") == 0);
    if (std::strcmp(parser->attrbuf, "DPScannedImage") == 0) {
      // Each DPScannedImage node claims the next slot of the fixed-capacity
      // `isyntax->images` array. The header controls how many such nodes it
      // declares, so without this bound it can write past the array.
      if (isyntax->image_count >= ISYNTAX_MAX_IMAGES) {
        SetXmlErrorOnce(
            isyntax,
            AIFOCORE_MAKE_STATUS(
                aifocore::StatusCode::kResourceExhausted,
                aifocore::fmt::format(
                    "iSyntax XML error: more than {} DPScannedImage objects",
                    ISYNTAX_MAX_IMAGES)));
        return false;
      }
      parser->current_image = isyntax->images + isyntax->image_count;
      parser->running_image_index = isyntax->image_count++;
    }
  } else {
    // Ignore other attributes.
  }
  ++parser->attribute_index;
  return true;
}

static void CleanupOnFailureOrEnd(isyntax_t* isyntax, bool success) {
  isyntax_xml_parser_t* parser = &isyntax->parser;
  parser->x = nullptr;
  parser->attrbuf = nullptr;
  parser->attrbuf_end = nullptr;
  parser->attrcur = nullptr;
  parser->attrlen = 0;
  parser->attrbuf_capacity = 0;
  parser->contentbuf = nullptr;
  parser->contentcur = nullptr;
  parser->contentlen = 0;
  parser->contentbuf_capacity = 0;
  parser->content_file_offset = 0;
  parser->initialized = false;

  delete isyntax->xml_cpp_state;
  isyntax->xml_cpp_state = nullptr;

  (void)success;
}

}  // namespace

namespace isyntax {

aifocore::Status ParseXmlHeaderChunk(isyntax_t* isyntax,
                                     std::span<const char> chunk,
                                     int64_t chunk_offset, bool is_last_chunk) {
  if (isyntax == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "isyntax is null");
  }

  if (isyntax->xml_cpp_state == nullptr) {
    isyntax->xml_cpp_state = new isyntax_xml_cpp_state_t();

    // Ensure parser bookkeeping is initialized.
    isyntax_xml_parser_t* parser = &isyntax->parser;
    parser->initialized = true;
    SyncParserPointersFromState(parser, isyntax->xml_cpp_state);
    parser->attrcur = nullptr;
    parser->contentcur = nullptr;
    parser->node_stack_index = 0;
    parser->data_object_stack_index = -1;
    parser->data_object_flags = 0;
    parser->block_header_template_index = 0;
    parser->cluster_header_template_index = 0;
    parser->block_header_index_for_cluster = 0;
    parser->dimension_index = 0;
    parser->valid_data_envelope_index = 0;
    parser->current_dicom_attribute_name[0] = '\0';
    parser->current_dicom_group_tag = 0;
    parser->current_dicom_element_tag = 0;
    parser->attribute_index = 0;
    parser->current_node_type = ISYNTAX_NODE_NONE;
    parser->current_node_has_children = false;
  } else {
    // Vectors may have moved; always refresh pointers.
    SyncParserPointersFromState(&isyntax->parser, isyntax->xml_cpp_state);
  }

  isyntax_xml_parser_t* parser = &isyntax->parser;
  yxml_t* x = parser->x;

  const char* xml_header = chunk.data();
  const char* doc = xml_header;

  yxml_ret_t prev_r = YXML_OK;
  for (int64_t remaining_length = static_cast<int64_t>(chunk.size());
       remaining_length > 0; --remaining_length, ++doc) {
    int c = static_cast<unsigned char>(*doc);
    if ((isyntax->open_flags & ISYNTAX_OPEN_FLAG_READ_BARCODE_ONLY) &&
        isyntax->is_barcode_read) {
      CleanupOnFailureOrEnd(isyntax, false);
      return aifocore::Status(aifocore::StatusCode::kCancelled,
                              "Barcode-only early abort");
    }

    yxml_ret_t r = yxml_parse(x, c);
    if (r == YXML_OK) {
      continue;
    }
    if (r < 0) {
      if (r == YXML_ESYN && c == '\0') {
        if (prev_r == YXML_ELEMEND && parser->node_stack_index == 0) {
          CleanupOnFailureOrEnd(isyntax, true);
          return aifocore::Status::OkStatus();
        }
      }
      CleanupOnFailureOrEnd(isyntax, false);
      return aifocore::Status(
          aifocore::StatusCode::kDataLoss,
          aifocore::fmt::format("XML parse error ({})", static_cast<int>(r)));
    }

    switch (r) {
      case YXML_ELEMSTART: {
        if (!HandleElemStart(isyntax, x)) {
          aifocore::Status handler_st = aifocore::Status::OkStatus();
          if (isyntax->xml_cpp_state != nullptr) {
            handler_st = isyntax->xml_cpp_state->last_error;
          }
          CleanupOnFailureOrEnd(isyntax, false);
          if (!handler_st.ok()) {
            return handler_st;
          }
          return aifocore::Status(aifocore::StatusCode::kDataLoss,
                                  "XML parse error: elemstart handler failed");
        }
      } break;

      case YXML_CONTENT: {
        int64_t bytes_to_skip = 0;
        if (!HandleContent(isyntax, isyntax->xml_cpp_state, x, chunk_offset,
                           xml_header, doc, remaining_length, &bytes_to_skip)) {
          CleanupOnFailureOrEnd(isyntax, false);
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kDataLoss,
              "XML parse error: content handler failed");
        }
        if (bytes_to_skip > 0) {
          doc += bytes_to_skip - 1;
          remaining_length -= bytes_to_skip - 1;
        }
      } break;

      case YXML_ELEMEND: {
        if (!HandleElemEnd(isyntax)) {
          aifocore::Status handler_st = aifocore::Status::OkStatus();
          if (isyntax->xml_cpp_state != nullptr) {
            handler_st = isyntax->xml_cpp_state->last_error;
          }
          CleanupOnFailureOrEnd(isyntax, false);
          if (!handler_st.ok()) {
            return handler_st;
          }
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kDataLoss,
              "XML parse error: elemend handler failed");
        }
      } break;

      case YXML_ATTRSTART: {
        parser->attrcur = parser->attrbuf;
        parser->attrbuf[0] = '\0';
        parser->attrlen = 0;
      } break;

      case YXML_ATTRVAL: {
        if (!HandleAttrVal(parser, isyntax->xml_cpp_state, x)) {
          aifocore::Status handler_st = aifocore::Status::OkStatus();
          if (isyntax->xml_cpp_state != nullptr) {
            handler_st = isyntax->xml_cpp_state->last_error;
          }
          CleanupOnFailureOrEnd(isyntax, false);
          if (!handler_st.ok()) {
            return handler_st;
          }
          return aifocore::Status(aifocore::StatusCode::kDataLoss,
                                  "XML parse error: attrval handler failed");
        }
      } break;

      case YXML_ATTREND: {
        if (!HandleAttrEnd(isyntax, x)) {
          aifocore::Status handler_st = aifocore::Status::OkStatus();
          if (isyntax->xml_cpp_state != nullptr) {
            handler_st = isyntax->xml_cpp_state->last_error;
          }
          CleanupOnFailureOrEnd(isyntax, false);
          if (!handler_st.ok()) {
            return handler_st;
          }
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kDataLoss,
              "XML parse error: attrend handler failed");
        }
      } break;

      case YXML_PISTART:
      case YXML_PICONTENT:
      case YXML_PIEND:
        break;

      default:
        CleanupOnFailureOrEnd(isyntax, false);
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            aifocore::fmt::format("yxml_parse(): unrecognized token ({})",
                                  static_cast<int>(r)));
    }
    prev_r = r;
  }

  if (is_last_chunk) {
    CleanupOnFailureOrEnd(isyntax, true);
  }
  return aifocore::Status::OkStatus();
}

}  // namespace isyntax

namespace isyntax {

void DestroyXmlCppState(isyntax_t* isyntax) {
  if (isyntax == nullptr) {
    return;
  }
  if (isyntax->xml_cpp_state == nullptr) {
    return;
  }
  CleanupOnFailureOrEnd(isyntax, true);
}

}  // namespace isyntax
