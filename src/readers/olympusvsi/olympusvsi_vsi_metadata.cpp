// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#include "fastslide/readers/olympusvsi/olympusvsi_vsi_metadata.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fastslide::formats::olympusvsi {
namespace {

// --- Olympus tag tree constants ---------------------------------------
//
// Each value below is a numeric field id stored in the `.vsi` container.
// The symbol names describe the observed role of each id, i.e. what the
// field is seen to carry. The ids and their meaning can be reproduced with
// the companion tool `inspect_vsi_format.py`, which walks the tag tree
// structurally and pins each id by correlating its payload against ground
// truth.

// Field-id tags whose payloads we surface to the reader.
constexpr int32_t kFieldChannelLabel = 2419;  // Text: acquisition channel name.
constexpr int32_t kFieldLayerLabel = 2030;    // Text: stack/layer display name.
constexpr int32_t kFieldBoundaryRect = 2053;  // int32 rect (x, y, w, h).
// Two doubles (X, Y) = microns-per-pixel for the enclosing image frame.
// Carried per-frame, so navigator/overview and scan regions each report
// their own value (unrelated to the container TIFF resolution).
constexpr int32_t kFieldMicronScale = 2019;

// Document-properties text field carrying the acquisition software / device
// name (observed as e.g. "OLYMPUS VS200 ASW"). It lives in the document
// scope, once per container rather than per image.
constexpr int32_t kFieldDeviceName = 34;

// Field-id tags that act purely as structural delimiters: they segment the
// flat field stream into per-image groups. An image group opens when the
// external-pixels marker is seen directly after the frame-group marker; the
// document- and slide-scope markers close any open group. These four ids are
// also recovered empirically by `inspect_vsi_format.py` (Phase 8): the
// frame-group marker is the container two fields before each image boundary,
// the external-pixels marker is the container preceding a boundary that
// matches a measured `.ets` pyramid, and the scope closers are the
// frame-group-depth containers that appear only after the last image content.
constexpr int32_t kFrameGroupMarker = 2002;
constexpr int32_t kExternalPixelsMarker = 2018;
constexpr int32_t kDocumentScopeMarker = 2109;
constexpr int32_t kSlideScopeMarker = 2062;

// "Real type" of a data field (low 24 bits of the field type word). The
// three nested kinds all introduce sub-volumes; the sequence variant holds a
// run of sibling sub-volumes while the two single variants hold exactly one.
constexpr uint32_t kKindNestedSequence = 0;
constexpr uint32_t kKindNestedSingleA = 1;
constexpr uint32_t kKindNestedSingleB = 2;
constexpr uint32_t kKindAsciiText = 13;
constexpr uint32_t kKindColorRgb = 269;
constexpr uint32_t kKindColorBgr = 270;
constexpr uint32_t kKindUtf16Text = 8192;

// Field-type bit flags.
constexpr uint32_t kFlagExtraTag = 0x08000000u;
constexpr uint32_t kFlagExtendedField = 0x10000000u;
constexpr uint32_t kFlagInlineData = 0x40000000u;
constexpr uint32_t kRealTypeMask = 0x00ffffffu;

// The low 28 bits of the volume-header flag word hold the field count.
constexpr uint32_t kFieldCountMask = 0x0fffffffu;

// Safety limits for a best-effort cosmetic parse.
constexpr uint64_t kMaxVsiBytes = 256ull * 1024 * 1024;  // 256 MiB.
constexpr int kMaxRecursionDepth = 32;

/// @brief Decode a UTF-16LE byte run (BMP only) into UTF-8, dropping NULs.
std::string DecodeUtf16Le(const uint8_t* data, size_t n_bytes) {
  std::string out;
  out.reserve(n_bytes / 2);
  for (size_t i = 0; i + 1 < n_bytes; i += 2) {
    const uint32_t cp = static_cast<uint32_t>(data[i]) |
                        (static_cast<uint32_t>(data[i + 1]) << 8);
    if (cp == 0) {
      continue;
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

/// @brief Decode a single-byte (TCHAR) run, dropping NULs.
std::string DecodeAscii(const uint8_t* data, size_t n_bytes) {
  std::string out;
  out.reserve(n_bytes);
  for (size_t i = 0; i < n_bytes; ++i) {
    if (data[i] != 0) {
      out.push_back(static_cast<char>(data[i]));
    }
  }
  return out;
}

/// @brief Read a little-endian IEEE-754 double from a byte run.
double ReadLeDouble(const uint8_t* p) {
  uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) {
    bits |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  double out = 0.0;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

void Trim(std::string& s) {
  const auto not_space = [](unsigned char c) {
    return c != ' ' && c != '\t';
  };
  while (!s.empty() && !not_space(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  size_t start = 0;
  while (start < s.size() && !not_space(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  s.erase(0, start);
}

/// @brief Bounds-checked little-endian cursor over an in-memory buffer.
class Cursor {
 public:
  explicit Cursor(const std::vector<uint8_t>& buf) : buf_(buf) {}

  [[nodiscard]] size_t Tell() const { return pos_; }

  void Seek(size_t p) { pos_ = p; }

  [[nodiscard]] size_t Size() const { return buf_.size(); }

  [[nodiscard]] bool Remaining(size_t n) const {
    return pos_ + n <= buf_.size();
  }

  uint16_t U16() {
    uint16_t v = static_cast<uint16_t>(buf_[pos_]) |
                 (static_cast<uint16_t>(buf_[pos_ + 1]) << 8);
    pos_ += 2;
    return v;
  }

  int32_t I32() { return static_cast<int32_t>(U32()); }

  uint32_t U32() {
    uint32_t v = static_cast<uint32_t>(buf_[pos_]) |
                 (static_cast<uint32_t>(buf_[pos_ + 1]) << 8) |
                 (static_cast<uint32_t>(buf_[pos_ + 2]) << 16) |
                 (static_cast<uint32_t>(buf_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
  }

  int64_t I64() {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
      v |= static_cast<uint64_t>(buf_[pos_ + i]) << (8 * i);
    }
    pos_ += 8;
    return static_cast<int64_t>(v);
  }

  [[nodiscard]] const uint8_t* At(size_t p) const { return buf_.data() + p; }

 private:
  const std::vector<uint8_t>& buf_;
  size_t pos_ = 0;
};

/// @brief One field captured by the structural walk, decoupled from any
///        interpretation.
///
/// The parser runs in two passes (mirroring how `inspect_vsi_format.py`
/// derives the layout): a first pass flattens the nested tag tree into a
/// document-ordered list of these records using only the self-describing
/// structure, and a second pass assigns their payloads to images. Keeping
/// the two passes separate means the structural walk never needs to know
/// what any tag *means*.
struct RawField {
  int32_t tag = 0;
  uint32_t kind = 0;  ///< Field "real type" (low 24 bits of type word).
  bool is_container = false;
  std::vector<uint8_t> payload;  ///< Empty for containers / inline / zero-size.
};

/// @brief Pull the brightest-entry colour from an RGB/BGR array field.
///
/// Handles both a single display-colour triple (3 bytes) and a full
/// 256-entry display LUT (768 bytes): the channel's representative colour
/// is the last entry (max intensity ramp endpoint).
std::optional<ColorRGB> EndpointColor(const uint8_t* data, size_t n_bytes,
                                      bool is_bgr) {
  if (n_bytes < 3) {
    return std::nullopt;
  }
  const size_t triples = n_bytes / 3;
  const uint8_t* last = data + (triples - 1) * 3;
  if (is_bgr) {
    return ColorRGB{last[2], last[1], last[0]};
  }
  return ColorRGB{last[0], last[1], last[2]};
}

/// @brief Decoded type word of a data field (format-fact bit layout).
struct FieldType {
  bool extra_tag = false;
  bool extended = false;
  bool inline_data = false;
  uint32_t real_type = 0;
};

FieldType DecodeFieldType(uint32_t word) {
  FieldType t;
  t.extra_tag = (word & kFlagExtraTag) != 0;
  t.extended = (word & kFlagExtendedField) != 0;
  t.inline_data = (word & kFlagInlineData) != 0;
  t.real_type = word & kRealTypeMask;
  return t;
}

/// @brief One decoded data-field header.
struct FieldHeader {
  FieldType type;
  int32_t tag = 0;
  uint32_t next_field = 0;
  uint32_t data_size = 0;
};

/// @brief Read the 24-byte container header at @p volume_start, leave the
///        cursor on the first data field, and report the field count.
///
/// @return ``false`` (caller should stop) when the header is truncated or
///         the declared field region is out of range.
bool ReadVolumeHeader(Cursor& c, size_t volume_start, uint32_t* field_count) {
  if (!c.Remaining(24) || volume_start + 24 >= c.Size()) {
    return false;
  }
  c.U16();  // Header size is always 24 in samples
  c.U16();  // Version is always 21321 in samples
  c.U32();  // Seems to be a reserved field
  const int64_t data_field_offset = c.I64();
  const uint32_t flags = c.U32();
  c.U32();  // Seems to be a reserved field

  *field_count = flags & kFieldCountMask;
  const int64_t data_field =
      static_cast<int64_t>(volume_start) + data_field_offset;
  if (data_field < 0 || static_cast<uint64_t>(data_field) >= c.Size()) {
    return false;
  }
  c.Seek(static_cast<size_t>(data_field));
  return *field_count <= c.Size();
}

/// @brief Read one field header (16 bytes + optional secondary tag word).
///
/// @return ``false`` when the optional secondary tag is truncated.
bool ReadFieldHeader(Cursor& c, FieldHeader* out) {
  out->type = DecodeFieldType(c.U32());
  out->tag = c.I32();
  out->next_field = c.U32();
  out->data_size = c.U32();
  if (out->type.extra_tag) {
    if (!c.Remaining(4)) {
      return false;
    }
    c.I32();  // Secondary tag, unused by the cosmetic parse.
  }
  return true;
}

/// @brief Decode a TCHAR / UnicodeTChar payload into a trimmed UTF-8 string.
std::string DecodeText(uint32_t kind, const uint8_t* value, uint32_t n_bytes) {
  std::string s = kind == kKindUtf16Text ? DecodeUtf16Le(value, n_bytes)
                                         : DecodeAscii(value, n_bytes);
  Trim(s);
  return s;
}

// ===================================================================== //
// Pass 1: structural flattening                                         //
// ===================================================================== //

/// @brief Classify a field-type word as one of the three nested-container
///        kinds, or none.
[[nodiscard]] bool IsContainerKind(const FieldType& t) {
  return t.extended && (t.real_type == kKindNestedSequence ||
                        t.real_type == kKindNestedSingleA ||
                        t.real_type == kKindNestedSingleB);
}

/// @brief Flatten one container ("volume") and its descendants into
///        @p out, appending each field once in document (pre-order)
///        position.
///
/// This pass is deliberately semantics-free: it follows only the
/// self-describing structure (the 24-byte volume header, the per-field
/// ``next_field`` links relative to @p volume_start, and the type-word
/// bit flags). Container fields are emitted and then descended into; leaf
/// fields carry a copy of their payload bytes for the interpretation pass.
void CollectVolume(Cursor& c, std::vector<RawField>& out, int depth) {
  if (depth > kMaxRecursionDepth) {
    return;
  }
  const size_t volume_start = c.Tell();
  uint32_t field_count = 0;
  if (!ReadVolumeHeader(c, volume_start, &field_count)) {
    return;
  }

  for (uint32_t i = 0; i < field_count; ++i) {
    if (!c.Remaining(16) || c.Tell() + 16 >= c.Size()) {
      break;
    }
    FieldHeader fh;
    if (!ReadFieldHeader(c, &fh)) {
      break;
    }
    // A negative tag ends the current container (this also absorbs the
    // large-negative end-of-chain sentinels Olympus writes).
    if (fh.tag < 0) {
      return;
    }

    const FieldType& t = fh.type;
    const bool container = IsContainerKind(t);
    const bool has_payload = !container && !t.inline_data && fh.data_size > 0;
    if (has_payload && !c.Remaining(fh.data_size)) {
      break;
    }

    RawField field;
    field.tag = fh.tag;
    field.kind = t.real_type;
    field.is_container = container;
    if (has_payload) {
      const uint8_t* start = c.At(c.Tell());
      field.payload.assign(start, start + fh.data_size);
    }
    out.push_back(std::move(field));

    if (container) {
      // The cursor sits on the nested volume's header. The sequence kind
      // packs several sibling volumes inside ``data_size`` bytes; the
      // single kinds hold exactly one.
      if (t.real_type == kKindNestedSequence) {
        const size_t end_ptr = c.Tell() + fh.data_size;
        while (c.Tell() < end_ptr && c.Tell() < c.Size()) {
          const size_t before = c.Tell();
          CollectVolume(c, out, depth + 1);
          if (c.Tell() <= before) {
            break;
          }
        }
      } else {
        CollectVolume(c, out, depth + 1);
      }
    } else if (has_payload) {
      c.Seek(c.Tell() + fh.data_size);
    }

    if (fh.next_field == 0) {
      return;
    }
    const int64_t next = static_cast<int64_t>(volume_start) + fh.next_field;
    if (next >= 0 && static_cast<uint64_t>(next) < c.Size()) {
      c.Seek(static_cast<size_t>(next));
    } else {
      break;
    }
  }
}

// ===================================================================== //
// Pass 2: interpretation                                                //
// ===================================================================== //
//
// The flattened stream is cut into one contiguous span per image first,
// then each span is reduced independently. Keeping the segmentation
// (where does image N's data live?) separate from the reduction (what do
// image N's leaves mean?) means neither step needs a shared mutable
// cursor: image identity is the span's position in the list, and any
// per-image scratch (e.g. a colour awaiting its channel) stays local to
// one reduction and cannot bleed into the next image.

/// @brief Half-open span ``[begin, end)`` of the flattened field list whose
///        leaves all belong to a single image.
struct FieldSpan {
  size_t begin = 0;
  size_t end = 0;
};

/// @brief True when the ``(previous, current)`` tag pair opens a new
///        external-data image.
///
/// Olympus writes the frame-group marker immediately followed by the
/// external-pixels marker at the head of every externally-stored frame;
/// frames without external pixel data omit the second marker. This single
/// one-field look-behind is the only document-order fact the cut needs.
[[nodiscard]] bool OpensImage(int32_t prev_tag, int32_t tag) {
  return tag == kExternalPixelsMarker && prev_tag == kFrameGroupMarker;
}

/// @brief True when this tag seals any open image span.
///
/// The document- and slide-scope volumes follow the last frame, so their
/// trailing fields must fall outside every image span.
[[nodiscard]] bool SealsImage(int32_t tag) {
  return tag == kDocumentScopeMarker || tag == kSlideScopeMarker;
}

/// @brief Cut the flattened field list into one span per image.
///
/// A span opens at each image-opening boundary and is sealed by the next
/// opening boundary or by a scope marker (whichever comes first). Fields
/// before the first image, or after a scope marker with no following
/// image, are covered by no span and therefore ignored downstream.
[[nodiscard]] std::vector<FieldSpan> CutImageSpans(
    const std::vector<RawField>& fields) {
  std::vector<FieldSpan> spans;
  int32_t prev_tag = 0;
  bool open = false;
  for (size_t i = 0; i < fields.size(); ++i) {
    const int32_t tag = fields[i].tag;
    if (OpensImage(prev_tag, tag)) {
      if (open) {
        spans.back().end = i;  // The new image seals the previous one.
      }
      spans.push_back(FieldSpan{i, fields.size()});  // Provisional EOF end.
      open = true;
    } else if (open && SealsImage(tag)) {
      spans.back().end = i;
      open = false;
    }
    prev_tag = tag;
  }
  return spans;
}

/// @brief Fold one leaf field's payload into the image being built.
///
/// @param img            Image accumulating this span's fields.
/// @param pending_color  Most recent display colour/LUT endpoint, consumed
///                        by the next channel name. Local to one span.
void AbsorbLeaf(VsiPyramidMeta& img, std::optional<ColorRGB>& pending_color,
                const RawField& field) {
  const uint8_t* value = field.payload.data();
  const uint32_t n = static_cast<uint32_t>(field.payload.size());
  const bool is_text =
      field.kind == kKindAsciiText || field.kind == kKindUtf16Text;

  if (is_text && field.tag == kFieldChannelLabel) {
    VsiChannelMeta ch;
    ch.name = DecodeText(field.kind, value, n);
    ch.color = pending_color;  // nullopt unless a LUT/triple just preceded it.
    pending_color.reset();
    img.channels.push_back(std::move(ch));
  } else if (is_text && field.tag == kFieldLayerLabel) {
    std::string name = DecodeText(field.kind, value, n);
    if (img.name.empty() && name != "0") {
      img.name = std::move(name);
    }
  } else if (field.kind == kKindColorRgb || field.kind == kKindColorBgr) {
    // A display colour / LUT that precedes the next channel name.
    if (auto col = EndpointColor(value, n, field.kind == kKindColorBgr)) {
      pending_color = col;
    }
  } else if (field.tag == kFieldBoundaryRect && n >= 16 && img.width == 0) {
    // Boundary rectangle (x, y, w, h) as int32; used to pair this image
    // with a discovered `.ets` stack by size. Width/height are slots 2/3.
    const auto le32 = [value](size_t slot) {
      const size_t off = slot * 4;
      return static_cast<uint32_t>(value[off]) |
             (static_cast<uint32_t>(value[off + 1]) << 8) |
             (static_cast<uint32_t>(value[off + 2]) << 16) |
             (static_cast<uint32_t>(value[off + 3]) << 24);
    };
    img.width = le32(2);
    img.height = le32(3);
  } else if (field.tag == kFieldMicronScale && n >= 16 && img.mpp_x == 0.0) {
    // Per-image microns-per-pixel (X, Y). First occurrence wins so a later
    // coarse-level scale cannot overwrite the level-0 value.
    const double sx = ReadLeDouble(value);
    const double sy = ReadLeDouble(value + 8);
    if (sx > 0.0 && sy > 0.0) {
      img.mpp_x = sx;
      img.mpp_y = sy;
    }
  }
}

/// @brief Pull the document-level device / acquisition-software name from
///        the flattened field list.
///
/// The device-name field lives in the document scope, not inside any image
/// span, so it is read from the full field list. The first text-bearing
/// occurrence wins.
std::string ExtractDeviceName(const std::vector<RawField>& fields) {
  for (const RawField& field : fields) {
    if (field.tag != kFieldDeviceName || field.is_container ||
        field.payload.empty()) {
      continue;
    }
    if (field.kind != kKindAsciiText && field.kind != kKindUtf16Text) {
      continue;
    }
    std::string name = DecodeText(field.kind, field.payload.data(),
                                  static_cast<uint32_t>(field.payload.size()));
    if (!name.empty()) {
      return name;
    }
  }
  return {};
}

/// @brief Cut the flattened field list into per-image spans, then reduce
///        each span's leaves into one image.
std::vector<VsiPyramidMeta> ReduceFieldsToImages(
    const std::vector<RawField>& fields) {
  const std::vector<FieldSpan> spans = CutImageSpans(fields);
  std::vector<VsiPyramidMeta> images;
  images.reserve(spans.size());
  for (const FieldSpan& span : spans) {
    VsiPyramidMeta img;
    std::optional<ColorRGB> pending_color;
    for (size_t i = span.begin; i < span.end; ++i) {
      const RawField& field = fields[i];
      if (field.is_container || field.payload.empty()) {
        continue;
      }
      AbsorbLeaf(img, pending_color, field);
    }
    images.push_back(std::move(img));
  }
  return images;
}

}  // namespace

VsiContainerMeta ParseVsiMetadata(const std::filesystem::path& vsi_path) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(vsi_path, ec);
  if (ec || sz == 0 || sz > kMaxVsiBytes || sz < 32) {
    return {};
  }

  std::ifstream in(vsi_path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::vector<uint8_t> buf(static_cast<size_t>(sz));
  in.read(reinterpret_cast<char*>(buf.data()),
          static_cast<std::streamsize>(sz));
  if (!in) {
    return {};
  }

  // Pass 1: flatten the nested tag tree into a document-ordered field list
  // using only the format's self-describing structure. Pass 2: replay that
  // list to segment it into images and recover the fields we surface; the
  // document-level device name is pulled from the same flattened list.
  Cursor cursor(buf);
  cursor.Seek(8);  // Olympus tag tree begins after the 8-byte TIFF header.
  std::vector<RawField> fields;
  CollectVolume(cursor, fields, /*depth=*/0);

  VsiContainerMeta meta;
  meta.device_name = ExtractDeviceName(fields);
  meta.pyramids = ReduceFieldsToImages(fields);
  return meta;
}

}  // namespace fastslide::formats::olympusvsi
