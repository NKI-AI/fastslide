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

#include <cstdint>
#include <vector>

#include "aifocore/status/result.h"
#include "gtest/gtest.h"

// =============================================================================
// Third-party attribution
// =============================================================================
//
// This test validates OpenJPEG-derived sYCC edge behavior that is ported into
// SimpleTIFF. The behavior is modeled after OpenJPEG 2.4.0:
// - openjpeg-2.4.0/src/bin/common/color.c: color_sycc_to_rgb()
//
// OpenJPEG is licensed under the BSD 2-Clause License.
// See: openjpeg-2.4.0/LICENSE (in this repository).

// This test intentionally validates the ported OpenJPEG SYCC->RGB edge behavior
// inside SimpleTIFF by driving the public JPEG2000 decoder with tiny synthetic
// buffers is impractical. Instead, we validate the exact math/edge conditions
// by constructing a minimal opj_image_t in-memory and calling the internal
// packer.
//
// Note: this test is compiled only when OpenJPEG is available.

#if defined(SIMPLETIFF_HAS_OPENJPEG)

extern "C" {
#include "openjpeg.h"
}  // extern "C"

namespace simpletiff {

// Declared in aifo/simpletiff/src/jpeg2000.cpp for test-only use.
aifocore::Result<void> PackInterleavedForTest(
    const opj_image_t& image, bool file_big_endian,
    uint16_t expected_bits_per_sample, uint16_t expected_samples_per_pixel,
    bool convert_ycbcr_to_rgb, int& out_width, int& out_height,
    std::vector<uint8_t>& out);

namespace {

TEST(Jpeg2000SyccPackTest, Sycc422HonorsOddX0WithZeroCbCr) {
  // 4:2:2 pattern with odd x0 => first column uses Cb/Cr = 0 in OpenJPEG.
  opj_image_cmptparm_t cmptparm[3]{};
  for (auto& p : cmptparm) {
    p.prec = 8;
    p.bpp = 8;
    p.sgnd = 0;
    p.dx = 1;
    p.dy = 1;
    p.w = 3;
    p.h = 1;
  }
  // Cb/Cr are subsampled horizontally by 2.
  cmptparm[1].dx = 2;
  cmptparm[2].dx = 2;
  cmptparm[1].w = 2;  // ceil(3/2)
  cmptparm[2].w = 2;

  opj_image_t* img = opj_image_create(3, cmptparm, OPJ_CLRSPC_SYCC);
  ASSERT_NE(img, nullptr);
  img->x0 = 1;  // odd => triggers offx path
  img->y0 = 0;
  img->x1 = 4;
  img->y1 = 1;

  // Y values for 3 pixels.
  img->comps[0].data[0] = 100;
  img->comps[0].data[1] = 120;
  img->comps[0].data[2] = 130;

  // One chroma sample (neutral) that should apply to pixels 1 and 2.
  img->comps[1].data[0] = 128;
  img->comps[2].data[0] = 128;

  std::vector<uint8_t> out;
  int w = 0;
  int h = 0;
  auto r = PackInterleavedForTest(*img, /*file_big_endian=*/false,
                                  /*expected_bits_per_sample=*/8,
                                  /*expected_samples_per_pixel=*/3,
                                  /*convert_ycbcr_to_rgb=*/true, w, h, out);
  opj_image_destroy(img);
  ASSERT_TRUE(r) << r.error().ToString();
  ASSERT_EQ(w, 3);
  ASSERT_EQ(h, 1);
  ASSERT_EQ(out.size(), static_cast<size_t>(w * h * 3));

  // Pixel 0: cb/cr forced to 0 => strong tint (r=0, g high, b=0).
  EXPECT_EQ(out[0], 0u);    // R0
  EXPECT_GE(out[1], 200u);  // G0 (approx 235 with OpenJPEG truncation)
  EXPECT_EQ(out[2], 0u);    // B0

  // Pixels 1 and 2: neutral chroma => RGB == Y.
  EXPECT_EQ(out[3], 120u);
  EXPECT_EQ(out[4], 120u);
  EXPECT_EQ(out[5], 120u);

  EXPECT_EQ(out[6], 130u);
  EXPECT_EQ(out[7], 130u);
  EXPECT_EQ(out[8], 130u);
}

}  // namespace
}  // namespace simpletiff

#endif  // SIMPLETIFF_HAS_OPENJPEG
