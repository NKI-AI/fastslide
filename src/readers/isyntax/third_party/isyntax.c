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

/*
  Decoder for whole-slide image files in iSyntax format.

  This implementation is based on the documentation on the iSyntax format
  released by Philips: https://www.openpathology.philips.com/isyntax/

  See the following documents, and the accompanying source code samples:
  - "Fast Compression Method for Medical Images on the Web", by Bas Hulsken
    https://arxiv.org/abs/2005.08713
  - The description of the iSyntax image files:
    https://www.openpathology.philips.com/wp-content/uploads/isyntax/4522%20207%2043941_2020_04_24%20Pathology%20iSyntax%20image%20format.pdf

  This implementation does not require the Philips iSyntax SDK.

  NOTE (2023-03-29):
  Unfortunately, Philips' original OpenPathology website is no longer
  accessible. The documentation on the file format is still available from
  Philips, but has moved to another location:
  https://share.philips.com/sites/OpenPathologyPortal
  However, you have to contact Philips directly to get access there.

*/
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include <cstdio>

#include "fastslide/readers/isyntax/third_party/platform/common.h"
#include "fastslide/readers/isyntax/third_party/platform/intrinsics.h"

#include "fastslide/readers/isyntax/third_party/tile.h"
#include "fastslide/readers/isyntax/third_party/utils/mathutils.h"

// JPEG decoding library for macro/label images
#ifdef ISYNTAX_JPEG_DECODER_USE_LIBJPEG
#include "fastslide/readers/isyntax/third_party/jpeg_decoder.h"
#else
// #include "stb_image.h"
#endif

// Enable/disable debug routines for creating PNGs of IDWT steps
#define ISYNTAX_WANT_DEBUG_OUTPUT_PNG 0

#include <ctype.h>
#ifdef __cplusplus
#include <cstdlib>
#include <memory>
#endif

// NOLINTBEGIN
// Example codeblock order for a 'chunk' in the file:
// x        y        color   scale   coeff   offset       size    header_template_id
// 66302    66302    0       8       1       850048253    8270    18
// 65918    65918    0       7       1       850056531    17301   19
// 98686    65918    0       7       1       850073840    14503   19
// 65918    98686    0       7       1       850088351    8       19
// 98686    98686    0       7       1       850088367    8       19
// 65726    65726    0       6       1       850088383    26838   20
// 82110    65726    0       6       1       850115229    11215   20
// 98494    65726    0       6       1       850126452    6764    20
// 114878   65726    0       6       1       850133224    25409   20
// 65726    82110    0       6       1       850158641    21369   20
// 82110    82110    0       6       1       850180018    8146    20
// 98494    82110    0       6       1       850188172    4919    20
// 114878   82110    0       6       1       850193099    19908   20
// 65726    98494    0       6       1       850213015    8       20
// 82110    98494    0       6       1       850213031    8       20
// 98494    98494    0       6       1       850213047    8       20
// 114878   98494    0       6       1       850213063    8       20
// 65726    114878   0       6       1       850213079    8       20
// 82110    114878   0       6       1       850213095    8       20
// 98494    114878   0       6       1       850213111    8       20
// 114878   114878   0       6       1       850213127    8       20
// 66558    66558    0       8       0       850213143    5558    21    <-
// LL codeblock
// NOLINTEND

// The above pattern repeats for the other 2 color channels (1 and 2).
// The LL codeblock is only present at the highest scales.

// Codeblock decompression wrapper moved to C++ (`decompress.cpp`).

// Read between 57 and 64 bits (7 bytes + 1-8 bits) from a bitstream (least
// significant bit first). Requires that at least 7 safety bytes are present at
// the end of the stream (don't trigger a segmentation fault)!

// Note: open-pipeline geometry + codeblock processing helpers moved to C++20
// (`open_helpers.cpp`).

// Chunk layout helper moved to C++ (`chunk_layout.h`).

// Dump codeblock info from block header to a .csv file
static void isyntax_dump_block_header(isyntax_image_t* wsi_image,
                                      const char* filename) {
  if (filename == NULL) {
    filename = "test_block_header.csv";
  }
  FILE* test_block_header_fp = fopen(filename, "wb");
  if (test_block_header_fp) {
    fprintf(test_block_header_fp,
            "x_coordinate,y_coordinate,color_component,scale,coefficient,block_"
            "data_offset,block_data_size,block_header_template_id\n");

    for (int32_t i = 0; i < wsi_image->codeblock_count; i += 1 /*21*3*/) {
      isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
      fprintf(test_block_header_fp, "%d,%d,%d,%d,%d,%lld,%lld,%d\n",
              // codeblock->x_adjusted,
              // codeblock->y_adjusted,
              codeblock->x_coordinate - wsi_image->offset_x,
              codeblock->y_coordinate - wsi_image->offset_y,
              codeblock->color_component, codeblock->scale,
              codeblock->coefficient, codeblock->block_data_offset,
              codeblock->block_size, codeblock->block_header_template_id);
    }

    fclose(test_block_header_fp);
  }
}

/**
 * Read and process the seektable to populate codeblock offsets and sizes.
 *
 * @param fp File stream to read from (positioned at data offset)
 * @param isyntax iSyntax context structure
 * @param wsi_image WSI image containing the codeblocks
 * @return true on success, false on failure
 */
/**
 * Populate debug information for all tiles.
 *
 * @param wsi_image WSI image to populate debug info for
 */
void populate_tile_debug_info(isyntax_image_t* wsi_image) {
  for (int scale = 0; scale < wsi_image->level_count; ++scale) {
    isyntax_level_t* level = &wsi_image->levels[scale];
    for (int tile_y = 0; tile_y < level->height_in_tiles; ++tile_y) {
      for (int tile_x = 0; tile_x < level->width_in_tiles; ++tile_x) {
        isyntax_tile_t* tile =
            &level->tiles[level->width_in_tiles * tile_y + tile_x];
        tile->tile_scale = scale;
        tile->tile_x = tile_x;
        tile->tile_y = tile_y;
      }
    }
  }
}
