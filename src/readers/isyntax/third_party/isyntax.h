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

typedef struct isyntax_xml_cpp_state_t isyntax_xml_cpp_state_t;

#include <cstdint>
#include <cstdio>

#include "readers/isyntax/third_party/isyntax_types.h"
#include "readers/isyntax/third_party/platform/common.h"
#include "readers/isyntax/third_party/platform/platform.h"
#include "readers/isyntax/third_party/utils/block_allocator.h"

#include "readers/isyntax/third_party/third_party/yxml.h"

#define DWT_COEFF_BITS 16
#if (DWT_COEFF_BITS == 16)
typedef int16_t icoeff_t;
#else
typedef int32_t icoeff_t;
#endif

#define ISYNTAX_IDWT_PAD_L 4
#define ISYNTAX_IDWT_PAD_R 4
#define ISYNTAX_IDWT_FIRST_VALID_PIXEL 7

#define ISYNTAX_ADJ_TILE_TOP_LEFT 0x100
#define ISYNTAX_ADJ_TILE_TOP_CENTER 0x80
#define ISYNTAX_ADJ_TILE_TOP_RIGHT 0x40
#define ISYNTAX_ADJ_TILE_CENTER_LEFT 0x20
#define ISYNTAX_ADJ_TILE_CENTER 0x10
#define ISYNTAX_ADJ_TILE_CENTER_RIGHT 8
#define ISYNTAX_ADJ_TILE_BOTTOM_LEFT 4
#define ISYNTAX_ADJ_TILE_BOTTOM_CENTER 2
#define ISYNTAX_ADJ_TILE_BOTTOM_RIGHT 1

enum isyntax_image_type_enum {
  ISYNTAX_IMAGE_TYPE_NONE = 0,
  ISYNTAX_IMAGE_TYPE_MACROIMAGE = 1,
  ISYNTAX_IMAGE_TYPE_LABELIMAGE = 2,
  ISYNTAX_IMAGE_TYPE_WSI = 3,
};

enum isyntax_node_type_enum {
  ISYNTAX_NODE_NONE = 0,
  ISYNTAX_NODE_LEAF =
      1,  // ex. <Attribute Name="DICOM_MANUFACTURER" Group="0x0008"
          // Element="0x0070"PMSVR="IString">PHILIPS</ Attribute>
  ISYNTAX_NODE_BRANCH =
      2,  // ex. <DataObject ObjectType="DPScannedImage"> (leaf
          // nodes) </DataObject>
  ISYNTAX_NODE_ARRAY =
      3,  // <Array> (contains one or more similar type of leaf/branch nodes)
};

// NOTE: Most of these have DICOM group 0x301D. Currently there seem to be no
// element ID collisions.
enum isyntax_group_data_object_dicom_element_enum {
  // Group 0x301D
  PIM_DP_SCANNED_IMAGES = 0x1003,     // DPScannedImage
  DP_IMAGE_POST_PROCESSING = 0x1014,  // DPImagePostProcessing
  DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR =
      0x1019,  // DPWaveletQuantizerSeetingsPerColor
  DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL =
      0x101a,                          // DPWaveletQuantizerSeetingsPerLevel
  UFS_IMAGE_GENERAL_HEADERS = 0x2000,  // UFSImageGeneralHeader
  UFS_IMAGE_DIMENSIONS = 0x2003,       // UFSImageDimension
  UFS_IMAGE_BLOCK_HEADER_TEMPLATES = 0x2009,  // UFSImageBlockHeaderTemplate
  UFS_IMAGE_DIMENSION_RANGES = 0x200a,        // UFSImageDimensionRange
  DP_COLOR_MANAGEMENT = 0x200b,               // DPColorManagement
  UFS_IMAGE_BLOCK_HEADERS =
      0x200d,  // UFSImageBlockHeader              // new in iSyntax v2
  UFS_IMAGE_CLUSTER_HEADER_TEMPLATES =
      0x2016,  // UFSImageClusterHeaderTemplate    // new in iSyntax v2
  UFS_IMAGE_VALID_DATA_ENVELOPES =
      0x2023,  // UFSImageValidDataEnvelope        // new in iSyntax v2
  UFS_IMAGE_OPP_EXTREME_VERTICES =
      0x2024,  // UFSImageOppExtremeVertex         // new in iSyntax v2
  // Group 8B01
  PIIM_PIXEL_DATA_REPRESENTATION_SEQUENCE = 0x1001,  // PixelDataRepresentation
};

enum isyntax_data_object_flag_enum {
  ISYNTAX_OBJECT_DPUfsImport = 1,
  ISYNTAX_OBJECT_DPScannedImage = 2,
  ISYNTAX_OBJECT_UFSImageGeneralHeader = 4,
  ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate = 8,
  ISYNTAX_OBJECT_UFSImageDimension = 0x10,
  ISYNTAX_OBJECT_UFSImageDimensionRange = 0x20,
  ISYNTAX_OBJECT_DPColorManagement = 0x40,
  ISYNTAX_OBJECT_DPImagePostProcessing = 0x80,
  ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor = 0x100,
  ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel = 0x200,
  ISYNTAX_OBJECT_PixelDataRepresentation = 0x400,
  ISYNTAX_OBJECT_UFSImageBlockHeader = 0x800,             // new in iSyntax v2
  ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate = 0x1000,  // new in iSyntax v2
  ISYNTAX_OBJECT_UFSImageValidDataEnvelope = 0x2000,      // new in iSyntax v2
  ISYNTAX_OBJECT_UFSImageOppExtremeVertex = 0x4000,       // new in iSyntax v2
};

#pragma pack(push, 1)

typedef struct isyntax_dicom_tag_header_t {
  uint16_t group;
  uint16_t element;
  uint32_t size;
} isyntax_dicom_tag_header_t;

typedef struct isyntax_partial_block_header_t {
  isyntax_dicom_tag_header_t sequence_element_header;
  isyntax_dicom_tag_header_t block_coordinates_header;
  uint32_t x_coordinate;
  uint32_t y_coordinate;
  uint32_t color_component;
  uint32_t scale;
  uint32_t coefficient;
  /* [MISSING] dicom_tag_header_t block_data_offset_header; */
  /* [MISSING] uint64_t block_data_offset; */
  /* [MISSING] dicom_tag_header_t block_size_header; */
  /* [MISSING] uint64_t block_size; */
  isyntax_dicom_tag_header_t block_header_template_id_header;
  uint32_t block_header_template_id;
} isyntax_partial_block_header_t;

typedef struct isyntax_full_block_header_t {
  isyntax_dicom_tag_header_t sequence_element_header;
  isyntax_dicom_tag_header_t block_coordinates_header;
  uint32_t x_coordinate;
  uint32_t y_coordinate;
  uint32_t color_component;
  uint32_t scale;
  uint32_t coefficient;
  isyntax_dicom_tag_header_t block_data_offset_header;
  uint64_t block_data_offset;
  isyntax_dicom_tag_header_t block_size_header;
  uint64_t block_size;
  isyntax_dicom_tag_header_t block_header_template_id_header;
  uint32_t block_header_template_id;
} isyntax_full_block_header_t;

typedef struct isyntax_seektable_codeblock_header_t {
  isyntax_dicom_tag_header_t start_header;
  isyntax_dicom_tag_header_t block_data_offset_header;
  uint64_t block_data_offset;
  isyntax_dicom_tag_header_t block_size_header;
  uint64_t block_size;
} isyntax_seektable_codeblock_header_t;

#pragma pack(pop)

typedef struct isyntax_image_dimension_range_t {
  int32_t start;
  int32_t step;
  int32_t end;
  int32_t numsteps;
} isyntax_image_dimension_range_t;

typedef struct isyntax_block_header_template_t {
  uint32_t block_width;     // e.g. 128
  uint32_t block_height;    // e.g. 128
  uint8_t color_component;  // 0=Y 1=Co 2=Cg
  uint8_t scale;            // range 0-8
  uint8_t waveletcoeff;     // either 1 for LL, or 3 for LH+HL+HH
} isyntax_block_header_template_t;

typedef struct isyntax_cluster_block_header_t {
  uint32_t x_coordinate;
  uint32_t y_coordinate;
  uint32_t color_component;
  uint32_t scale;
  uint32_t coefficient;
} isyntax_cluster_block_header_t;

typedef struct isyntax_cluster_relative_coords_t {
  uint32_t raw_coords[5];
  uint32_t block_header_template_id;
  uint32_t x;
  uint32_t y;
  uint32_t color_component;
  uint32_t scale;
  uint32_t waveletcoeff;
} isyntax_cluster_relative_coords_t;

#define MAX_CODEBLOCKS_PER_CLUSTER \
  70  // NOTE: what is the actual maximum possible?

typedef struct isyntax_cluster_header_template_t {
  uint32_t base_x;
  uint32_t base_y;
  uint8_t base_scale;
  uint8_t base_waveletcoeff;
  uint8_t base_color_component;
  isyntax_cluster_relative_coords_t
      relative_coords_for_codeblock_in_cluster[MAX_CODEBLOCKS_PER_CLUSTER];
  int32_t codeblock_in_cluster_count;
  int32_t dimension_order[5];
  uint8_t dimension_count;
} isyntax_cluster_header_template_t;

typedef struct isyntax_valid_data_envelope_t {
  v2i vertices[64];
  int32_t vertex_count;
} isyntax_valid_data_envelope_t;

typedef struct isyntax_codeblock_t {
  uint32_t x_coordinate;
  uint32_t y_coordinate;
  uint32_t color_component;
  uint32_t scale;
  uint32_t coefficient;
  uint64_t block_data_offset;
  uint64_t block_size;
  uint32_t block_header_template_id;
  int32_t x_adjusted;
  int32_t y_adjusted;
  int32_t block_x;
  int32_t block_y;
  uint64_t block_id;
} isyntax_codeblock_t;

typedef struct isyntax_data_chunk_t {
  int64_t offset;
  uint32_t size;
  int32_t top_codeblock_index;
  int32_t codeblock_count_per_color;
  int32_t scale;
  int32_t level_count;
  uint8_t* data;
} isyntax_data_chunk_t;

typedef struct isyntax_tile_channel_t {
  icoeff_t* coeff_h;
  icoeff_t* coeff_ll;
  uint32_t neighbors_loaded;
} isyntax_tile_channel_t;

typedef struct isyntax_tile_t {
  uint32_t codeblock_index;
  uint32_t codeblock_chunk_index;
  uint32_t data_chunk_index;
  isyntax_tile_channel_t color_channels[3];
  uint32_t ll_invalid_edges;
  bool exists;
  bool has_ll;
  bool has_h;
  bool is_submitted_for_h_coeff_decompression;
  bool is_submitted_for_loading;
  bool is_loaded;

  // Cache management.
  // TODO(avirodov): need to rethink this, maybe an external struct that points
  // to isyntax_tile_t. The benefit
  //   is that the cache is usually smaller than the number of tiles. The con is
  //   that I'll need to manage list memory (probably another allocator for
  //   small objects - list nodes).
  bool cache_marked;
  struct isyntax_tile_t* cache_next;
  struct isyntax_tile_t* cache_prev;

  // Note(avirodov): this is needed for isyntax_reader. It is very convenient to
  // be able to compute neighbors from the tile itself, although at the cost of
  // additional memory (3 ints) per tile.
  // TODO(avirodov): reconsider this as part of moving out cache_* fields, if
  // applicable.
  // TODO(avirodov): tile_x and tile_y can be computed by O(1) pointer
  // arithmetic given scale. Scale can be
  //  computed as well, but in O(L) where L is number of levels, and that will
  //  be computed often.
  int tile_scale;
  int tile_x;
  int tile_y;
} isyntax_tile_t;

typedef struct isyntax_level_t {
  int32_t scale;
  int32_t width_in_tiles;
  int32_t height_in_tiles;
  int32_t width;
  int32_t height;
  float downsample_factor;
  float um_per_pixel_x;
  float um_per_pixel_y;
  float x_tile_side_in_um;
  float y_tile_side_in_um;
  uint64_t tile_count;
  int32_t origin_offset_in_pixels;
  v2f origin_offset;
  isyntax_tile_t* tiles;
  bool is_fully_loaded;
} isyntax_level_t;

typedef struct isyntax_image_t {
  uint32_t image_type;
  int64_t base64_encoded_jpg_file_offset;
  size_t base64_encoded_jpg_len;
  int32_t width_including_padding;
  int32_t height_including_padding;
  int32_t width;
  int32_t height;
  int32_t level0_padding;
  int32_t offset_x;
  int32_t offset_y;
  int32_t level_count;
  int32_t max_scale;
  isyntax_level_t levels[16];
  int32_t compressor_version;
  bool compression_is_lossy;
  int32_t lossy_image_compression_ratio;
  int32_t number_of_blocks;
  int32_t codeblock_count;
  isyntax_codeblock_t* codeblocks;
  int32_t data_chunk_count;
  isyntax_data_chunk_t* data_chunks;
  bool header_codeblocks_are_partial;
  bool first_load_complete;
  bool first_load_in_progress;
  int64_t base64_encoded_icc_profile_file_offset;
  size_t base64_encoded_icc_profile_len;
} isyntax_image_t;

typedef struct isyntax_parser_node_t {
  uint32_t node_type;  // leaf, branch, or array
  bool has_children;
  bool has_base64_content;
  uint16_t group;
  uint16_t element;
} isyntax_parser_node_t;

#define ISYNTAX_MAX_NODE_DEPTH 16

typedef struct isyntax_xml_parser_t {
  yxml_t* x;
  isyntax_image_t* current_image;
  int32_t running_image_index;
  uint32_t current_image_type;
  char* attrbuf;
  char* attrbuf_end;
  char* attrcur;
  size_t attrlen;
  size_t attrbuf_capacity;
  char* contentbuf;
  char* contentcur;
  size_t contentlen;
  size_t contentbuf_capacity;
  int64_t content_file_offset;
  // For large content where we skip buffering, we still need basic metadata.
  char content_last_char;
  bool content_last_char_valid;
  bool content_was_skipped;
  char current_dicom_attribute_name[256];
  uint32_t current_dicom_group_tag;
  uint32_t current_dicom_element_tag;
  int32_t attribute_index;
  uint32_t current_node_type;
  bool current_node_has_children;
  isyntax_parser_node_t node_stack[ISYNTAX_MAX_NODE_DEPTH];
  int32_t node_stack_index;
  isyntax_parser_node_t data_object_stack[ISYNTAX_MAX_NODE_DEPTH];
  int32_t data_object_stack_index;
  uint32_t data_object_flags;
  int32_t block_header_template_index;
  int32_t cluster_header_template_index;
  int32_t block_header_index_for_cluster;
  int32_t dimension_index;
  int32_t valid_data_envelope_index;
  bool initialized;
} isyntax_xml_parser_t;

// Forward declaration: defined in `reader.h`.
typedef struct isyntax_cache_t isyntax_cache_t;

typedef struct isyntax_t {
  isyntax_open_flags_t open_flags;
  int64_t filesize;
  file_handle_t file_handle;
  isyntax_image_t images[16];
  int32_t image_count;
  isyntax_block_header_template_t block_header_templates[64];
  int32_t block_header_template_count;
  isyntax_cluster_header_template_t cluster_header_templates[8];
  int32_t cluster_header_template_count;
  isyntax_valid_data_envelope_t valid_data_envelopes[16];
  int32_t valid_data_envelope_count;
  int32_t macro_image_index;
  int32_t label_image_index;
  int32_t wsi_image_index;
  isyntax_xml_parser_t parser;
  float mpp_x;
  float mpp_y;
  bool is_mpp_known;
  int32_t block_width;
  int32_t block_height;
  int32_t tile_width;
  int32_t tile_height;
  icoeff_t* black_dummy_coeff;
  icoeff_t* white_dummy_coeff;
  block_allocator_t* ll_coeff_block_allocator;
  block_allocator_t* h_coeff_block_allocator;
  bool is_block_allocator_owned;
  float loading_time;
  float total_rgb_transform_time;
  int32_t data_model_major_version;  // <100 (usually 5) for iSyntax format v1,
                                     // >= 100 for iSyntax format v2
  char barcode[64];
  bool is_barcode_read;
  // Controls additional attribute validation during XML parsing (debug aid).
  // Keep this per-instance (not a global) so it’s testable and thread-safe.
  bool xml_paranoid_mode;
  // C++ XML parsing state (owned). NULL if the C++ parser hasn't been used.
  isyntax_xml_cpp_state_t* xml_cpp_state;
  isyntax_cache_t* cache;
  //  work_queue_t *work_submission_queue;
  volatile int32_t refcount;
  FILE* xml_dump_file;  // For dumping XML header (debug)
} isyntax_t;

// function prototypes
// void isyntax_set_work_queue(isyntax_t *isyntax, work_queue_t *work_queue);

#ifdef __cplusplus

#include <cstddef>
#include <span>

namespace isyntax {

namespace dwt {

// Inverse DWT (5/3) for a 2*quadrant_width x 2*quadrant_height coefficient
// tile.
void Idwt53(std::span<icoeff_t> idwt, int32_t quadrant_width,
            int32_t quadrant_height);

}  // namespace dwt

// C++ convenience constants mirroring the legacy C macros.
inline constexpr int kIdwtPadL = ISYNTAX_IDWT_PAD_L;
inline constexpr int kIdwtPadR = ISYNTAX_IDWT_PAD_R;
inline constexpr int kIdwtFirstValidPixel = ISYNTAX_IDWT_FIRST_VALID_PIXEL;

// Span helpers for coefficient blocks.
inline std::span<icoeff_t> CoeffSpan(icoeff_t* ptr, std::size_t count) {
  return std::span<icoeff_t>(ptr, count);
}

inline std::span<const icoeff_t> CoeffSpan(const icoeff_t* ptr,
                                           std::size_t count) {
  return std::span<const icoeff_t>(ptr, count);
}

}  // namespace isyntax

#endif  // __cplusplus
