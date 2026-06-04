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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_H_

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/olympusvsi/olympusvsi_ets.h"
#include "fastslide/readers/olympusvsi/olympusvsi_level_info.h"
#include "fastslide/readers/olympusvsi/olympusvsi_vsi_metadata.h"
#include "fastslide/slide_image.h"
#include "fastslide/slide_reader.h"
#include "simpletiff/index.h"

namespace fastslide::formats::olympusvsi {

class OlympusVsiReader;

/// @brief One pyramid in an Olympus VSI slide (one `stack*/frame_t.ets`).
///
/// Each Olympus VSI file ships several independent pyramids: a low-resolution
/// "navigator" (numbered ``stack1``, ``stack2``, ...) and one main scan per
/// imaged region (numbered ``stack10001``, ``stack10002``, ...). Each becomes
/// its own `SlideImage`.
///
/// The image holds its parsed `EtsFileData` + the per-level tile map. The
/// owning `OlympusVsiReader` supplies the optional tile cache via the
/// `cache_provider_` back-reference; cache lookups still key by the per-stack
/// `ets_path`, so multiple images cache independently.
class OlympusVsiStackImage final : public SlideImage {
 public:
  OlympusVsiStackImage(const OlympusVsiReader& cache_provider, std::string name,
                       EtsFileData ets,
                       std::vector<OlympusVsiLevelInfo> pyramid);

  [[nodiscard]] std::string GetName() const override { return name_; }

  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;

  [[nodiscard]] const SlideProperties& GetProperties() const override {
    return properties_;
  }

  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;

  /// @brief Image format inferred from the effective channel count.
  ///
  /// For 8-bit brightfield the count is the decoder-native RGB layout
  /// (``3`` → `kRGB`). For 16-bit fluorescence the count is the number
  /// of stacked grayscale planes: ``1`` → `kGray`, ``> 1`` → `kSpectral`
  /// (independent fluorophores, not colour components).
  [[nodiscard]] ImageFormat GetImageFormat() const override;

  /// @brief Pixel data type inferred from the declared ETS pixel type
  /// (``UCHAR`` → ``kUInt8``, ``USHORT`` → ``kUInt16``).
  [[nodiscard]] DataType GetDataType() const override;

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& canvas) const override;

  /// @brief Absolute path to the backing `frame_t.ets` file.
  [[nodiscard]] const std::filesystem::path& GetEtsPath() const {
    return ets_.path;
  }

  /// @brief Declared tile codec for this image (JPEG or JP2 in practice).
  [[nodiscard]] TileCodec GetCodec() const { return ets_.ets.compression; }

  /// @brief Effective channel count exposed by this image.
  ///
  /// For 8-bit brightfield this is the decoder-native RGB channel count
  /// (3). For 16-bit fluorescence it is the number of stacked grayscale
  /// planes discovered in the tile table (each plane is one output
  /// channel). Falls back to the declared ETS header count when the
  /// pyramid is empty.
  [[nodiscard]] uint32_t GetNChannels() const;

  /// @brief Attach real per-channel metadata (names + display colours)
  ///        resolved from the `.vsi` container tag tree.
  ///
  /// Only meaningful for multi-plane 16-bit fluorescence stacks. When the
  /// supplied vector's size matches the materialised plane count it
  /// overrides the generic ``"Channel N"`` fallback in
  /// `GetChannelMetadata()`.
  void SetChannelMetadata(std::vector<ChannelMetadata> channels) {
    channel_override_ = std::move(channels);
  }

  /// @brief Set this image's pixel size (microns-per-pixel) from the `.vsi`
  ///        container's per-frame micron-scale tag (2019).
  ///
  /// Each image frame (navigator, overview, scan region) carries its own
  /// scale, so this is set individually per stack rather than inherited from
  /// the container resolution.
  void SetMpp(double mpp_x, double mpp_y) {
    properties_.mpp[0] = mpp_x;
    properties_.mpp[1] = mpp_y;
  }

  /// @brief Replace the reported (API-facing) image size with the true
  ///        sub-tile boundary from the `.vsi` container.
  ///
  /// The on-disk ETS tile grid always rounds the scanned region up to a
  /// whole number of tiles, so the grid extent over-reports the real image
  /// by up to one tile on each axis. The exact boundary lives in the `.vsi`
  /// boundary-rect tag (2053) and is resolved per image frame. This sets the
  /// level-0 reported dimensions to ``(width, height)``, derives each
  /// coarser level by halving (round-up), and updates the slide bounds.
  ///
  /// Only the *reported* dimensions change: tile addressing and region
  /// reads still use the tile-aligned grid (`OlympusVsiLevelInfo::size`),
  /// and the plan builder crops the trailing edge tiles to the reported
  /// region. No-op when the pyramid is empty or either extent is zero.
  void SetReportedDimensions(uint32_t width, uint32_t height);

  /// @brief Override the scanner / acquisition-software model string.
  ///
  /// Sourced from the `.vsi` container's document-level device-name field
  /// (e.g. ``"OLYMPUS VS200 ASW"``). No-op for an empty string, so the
  /// constructor default is kept when the container omits the field.
  void SetScannerModel(std::string model) {
    if (!model.empty()) {
      properties_.scanner_model = std::move(model);
    }
  }

 private:
  /// @brief Number of materialised channel planes (1 unless this is a
  ///        multi-plane 16-bit fluorescence stack).
  [[nodiscard]] uint32_t PlaneCount() const {
    return pyramid_.empty() ? 1U : pyramid_.front().n_channels;
  }

  const OlympusVsiReader& cache_provider_;
  std::string name_;
  std::string ets_path_str_;
  EtsFileData ets_;
  std::vector<OlympusVsiLevelInfo> pyramid_;
  SlideProperties properties_;
  /// @brief Optional real channel metadata from the `.vsi` tag tree.
  ///        Empty unless a matching fluorescence pyramid was found.
  std::vector<ChannelMetadata> channel_override_;
};

/// @brief Olympus VSI brightfield-RGB reader (multi-image container).
///
/// Accepts a ``.vsi`` file (the TIFF container) or a ``frame_t.ets`` file
/// (a single Olympus stack). Every discovered ``stack*/frame_t.ets`` is
/// surfaced as a separate `SlideImage`; the primary image is the largest
/// main stack (folder name with a numeric suffix ``>= 10000``, falling back
/// to the largest stack overall).
///
/// MPP and macro/label associated images are read best-effort from the
/// ``.vsi`` TIFF container (when present); failures there never block
/// opening the slide.
class OlympusVsiReader : public SlideReader {
 public:
  static aifocore::Result<std::unique_ptr<OlympusVsiReader>> Create(
      std::string_view path);

  ~OlympusVsiReader() override = default;

  // -- Container API ----------------------------------------------------
  [[nodiscard]] int GetImageCount() const override {
    return static_cast<int>(images_.size());
  }

  [[nodiscard]] int GetPrimaryImageIndex() const override {
    return primary_index_;
  }

  [[nodiscard]] std::vector<std::string> GetImageNames() const override;
  [[nodiscard]] aifocore::Result<const SlideImage*> GetImage(
      int index) const override;

  // -- Primary-image forwarders (existing single-image surface) ---------
  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;

  [[nodiscard]] const SlideProperties& GetProperties() const override {
    return properties_;
  }

  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;
  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override;
  [[nodiscard]] aifocore::Result<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;
  [[nodiscard]] aifocore::Result<RGBImage> ReadAssociatedImage(
      std::string_view name) const override;

  [[nodiscard]] Metadata GetMetadata() const override;

  [[nodiscard]] std::string GetFormatName() const override {
    return "OLYMPUS-VSI";
  }

  /// @brief Image format / data type forwarded from the primary image.
  ///
  /// Primary picks the largest main stack (the brightfield RGB scan in
  /// mixed VSIs), so this matches the historical reader semantics for
  /// brightfield slides while still letting downstream consumers see
  /// `kGray` + `kUInt16` for pure-fluorescence VSIs.
  [[nodiscard]] ImageFormat GetImageFormat() const override;
  [[nodiscard]] DataType GetDataType() const override;

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& canvas) const override;

  [[nodiscard]] const std::string& GetFilename() const { return input_path_; }

  /// @brief One stack/ETS file that was discovered but not exposed as a
  ///        `SlideImage` (unsupported pixel type, codec, degenerate
  ///        pyramid, ...). The `reason` is the full
  ///        ``aifocore::Status::ToString()`` of the underlying failure.
  struct SkippedStack {
    std::string path;    ///< Absolute path to the offending ETS file.
    std::string reason;  ///< Human-readable failure description.
  };

  /// @brief Diagnostic list of stacks that were silently skipped during
  ///        ``Load()``. Useful for explaining "why didn't this image
  ///        show up?" without forcing the slide open to fail.
  [[nodiscard]] const std::vector<SkippedStack>& GetSkippedStacks() const {
    return skipped_stacks_;
  }

 private:
  struct AssociatedImage {
    std::string name;
    uint16_t page = 0;
    ImageDimensions size = {0, 0};
  };

  explicit OlympusVsiReader(std::string path);

  aifocore::Status Load();
  aifocore::Status DiscoverAndBuildImages();
  void PopulateAssociatedFromVsi();
  void EmitSkippedStackWarnings() const;

  /// @brief Find the `.vsi` container pyramid backing a discovered stack.
  ///
  /// Matches by tile count: Olympus stores each pyramid's true boundary
  /// (width/height), and the stack's level-0 grid is a whole number of
  /// tiles. A pyramid matches when ``ceil(boundary / tile)`` reproduces the
  /// grid's tile count on both axes. Matched pyramids are flagged in
  /// ``claimed`` so equal-sized regions pair up in document order rather
  /// than colliding.
  ///
  /// @param grid_w Level-0 grid width of the stack (tile-aligned px).
  /// @param grid_h Level-0 grid height of the stack (tile-aligned px).
  /// @param tile_w Tile width in px.
  /// @param tile_h Tile height in px.
  /// @param claimed Per-pyramid claim flags (mutated on match).
  /// @return Index into ``vsi_pyramids_``, or ``-1`` if none matched.
  [[nodiscard]] int MatchVsiPyramid(uint32_t grid_w, uint32_t grid_h,
                                    uint32_t tile_w, uint32_t tile_h,
                                    std::vector<bool>& claimed) const;

  [[nodiscard]] const OlympusVsiStackImage& Primary() const {
    return *images_[static_cast<size_t>(primary_index_)];
  }

  std::string input_path_;
  std::vector<std::unique_ptr<OlympusVsiStackImage>> images_;
  int primary_index_ = 0;
  /// @brief Per-stack diagnostics for ETS files that were discovered
  ///        but not exposed as a `SlideImage`. Mirrored into the
  ///        ``olympus_vsi.skipped[*].{path,reason}`` metadata keys.
  std::vector<SkippedStack> skipped_stacks_;

  SlideProperties properties_;
  std::unique_ptr<simpletiff::TiffIndex> vsi_tiff_;
  std::vector<AssociatedImage> associated_;
  /// @brief Per-image channel metadata parsed from the `.vsi` container
  ///        tag tree (channel names + display colours), in document order.
  ///        Empty for `.ets`-only inputs or when parsing yields nothing.
  std::vector<VsiPyramidMeta> vsi_pyramids_;
  /// @brief Document-level acquisition software / device name from the
  ///        `.vsi` container (e.g. "OLYMPUS VS200 ASW"). Empty when absent.
  std::string vsi_device_name_;
};

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_H_
