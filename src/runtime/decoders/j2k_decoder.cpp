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

#include "fastslide/runtime/decoders/j2k_decoder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <openjpeg.h>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide::runtime::decoders {

namespace {

struct OpjStreamData {
  const uint8_t* data;
  size_t size;
  size_t offset;
};

OPJ_SIZE_T OpjReadFn(void* buf, OPJ_SIZE_T nb_bytes, void* user) {
  auto* stream = static_cast<OpjStreamData*>(user);
  if (stream->offset >= stream->size)
    return static_cast<OPJ_SIZE_T>(-1);
  OPJ_SIZE_T avail = stream->size - stream->offset;
  OPJ_SIZE_T to_read = std::min(nb_bytes, avail);
  std::memcpy(buf, stream->data + stream->offset, to_read);
  stream->offset += to_read;
  return to_read;
}

OPJ_OFF_T OpjSkipFn(OPJ_OFF_T nb_bytes, void* user) {
  auto* stream = static_cast<OpjStreamData*>(user);
  if (nb_bytes < 0) {
    if (static_cast<size_t>(-nb_bytes) > stream->offset) {
      stream->offset = 0;
    } else {
      stream->offset -= static_cast<size_t>(-nb_bytes);
    }
  } else {
    stream->offset =
        std::min(stream->offset + static_cast<size_t>(nb_bytes), stream->size);
  }
  return nb_bytes;
}

OPJ_BOOL OpjSeekFn(OPJ_OFF_T nb_bytes, void* user) {
  auto* stream = static_cast<OpjStreamData*>(user);
  if (nb_bytes < 0 || static_cast<size_t>(nb_bytes) > stream->size) {
    return OPJ_FALSE;
  }
  stream->offset = static_cast<size_t>(nb_bytes);
  return OPJ_TRUE;
}

struct OpjCodecDeleter {
  void operator()(opj_codec_t* c) const {
    if (c)
      opj_destroy_codec(c);
  }
};

struct OpjStreamDeleter {
  void operator()(opj_stream_t* s) const {
    if (s)
      opj_stream_destroy(s);
  }
};

struct OpjImageDeleter {
  void operator()(opj_image_t* img) const {
    if (img)
      opj_image_destroy(img);
  }
};

}  // namespace

aifocore::Result<DecodedRgb> DecodeJ2kToRgb(
    std::span<const uint8_t> j2k_bytes) {
  OPJ_CODEC_FORMAT fmt = OPJ_CODEC_J2K;
  if (j2k_bytes.size() >= 12) {
    static constexpr uint8_t kJp2Sig[] = {0x00, 0x00, 0x00, 0x0C,
                                          0x6A, 0x50, 0x20, 0x20};
    if (std::memcmp(j2k_bytes.data(), kJp2Sig, sizeof(kJp2Sig)) == 0) {
      fmt = OPJ_CODEC_JP2;
    }
  }

  std::unique_ptr<opj_codec_t, OpjCodecDeleter> codec(
      opj_create_decompress(fmt));
  if (!codec) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to create OpenJPEG decoder");
  }

  opj_dparameters_t params;
  opj_set_default_decoder_parameters(&params);
  if (!opj_setup_decoder(codec.get(), &params)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to setup OpenJPEG decoder");
  }

  OpjStreamData stream_data{j2k_bytes.data(), j2k_bytes.size(), 0};
  std::unique_ptr<opj_stream_t, OpjStreamDeleter> stream(
      opj_stream_default_create(OPJ_TRUE));
  if (!stream) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to create OpenJPEG stream");
  }
  opj_stream_set_read_function(stream.get(), OpjReadFn);
  opj_stream_set_skip_function(stream.get(), OpjSkipFn);
  opj_stream_set_seek_function(stream.get(), OpjSeekFn);
  opj_stream_set_user_data(stream.get(), &stream_data, nullptr);
  opj_stream_set_user_data_length(stream.get(), j2k_bytes.size());

  opj_image_t* raw_image = nullptr;
  if (!opj_read_header(stream.get(), codec.get(), &raw_image)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to read JPEG 2000 header");
  }
  std::unique_ptr<opj_image_t, OpjImageDeleter> image(raw_image);

  if (!opj_decode(codec.get(), stream.get(), image.get())) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to decode JPEG 2000 image");
  }

  if (image->numcomps < 3) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Expected >= 3 components, got {}",
                              image->numcomps));
  }

  const uint32_t w = image->comps[0].w;
  const uint32_t h = image->comps[0].h;

  DecodedRgb result;
  result.width = w;
  result.height = h;
  result.rgb.resize(static_cast<size_t>(w) * h * 3);

  const OPJ_INT32* r_comp = image->comps[0].data;
  const OPJ_INT32* g_comp = image->comps[1].data;
  const OPJ_INT32* b_comp = image->comps[2].data;

  auto normalize = [](OPJ_INT32 val, const opj_image_comp_t& comp) -> uint8_t {
    if (comp.sgnd)
      val += (1 << (comp.prec - 1));
    if (comp.prec > 8) {
      val >>= (comp.prec - 8);
    } else if (comp.prec < 8) {
      val <<= (8 - comp.prec);
    }
    return static_cast<uint8_t>(std::clamp(val, OPJ_INT32{0}, OPJ_INT32{255}));
  };

  const size_t npx = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < npx; ++i) {
    result.rgb[i * 3 + 0] = normalize(r_comp[i], image->comps[0]);
    result.rgb[i * 3 + 1] = normalize(g_comp[i], image->comps[1]);
    result.rgb[i * 3 + 2] = normalize(b_comp[i], image->comps[2]);
  }

  return result;
}

}  // namespace fastslide::runtime::decoders
