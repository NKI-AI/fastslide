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

// TIFF index structures using Structure of Arrays (SoA) design
// for optimal cache locality and minimal allocations.

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INDEX_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INDEX_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace simpletiff {

/// Storage type for TIFF page data
enum class Storage : uint8_t {
  kTiles,       ///< Tiled storage
  kStrips,      ///< Strip-based storage
  kSingleJpeg,  ///< Single embedded JPEG
  kUnknown      ///< Unknown or unsupported storage
};

/// Represents a span into an arena vector (offset and count)
struct SpanU64 {
  uint32_t start = 0;  ///< Index into arena vector
  uint32_t count = 0;  ///< Number of elements
};

/// Lazy load metadata for deferred tag array reading
struct LazyArrayInfo {
  uint16_t tag_type = 0;      ///< TIFF type (3=SHORT, 4=LONG, 16=LONG8)
  uint64_t count = 0;         ///< Number of elements
  uint64_t value_offset = 0;  ///< File offset (or inline value)
  bool loaded = false;        ///< Whether array has been loaded
};

enum class JpegTablesState : uint8_t {
  kUninitialized = 0,
  kLoading = 1,
  kLoaded = 2,
  kFailed = 3,
};

/// Tiled storage record
struct TilesRec {
  uint16_t tile_w = 0;           ///< Tile width in pixels
  uint16_t tile_h = 0;           ///< Tile height in pixels
  uint32_t tiles_x = 0;          ///< Number of tiles horizontally
  uint32_t tiles_y = 0;          ///< Number of tiles vertically
  SpanU64 offsets;               ///< Slice of offsets_arena
  SpanU64 bytecounts;            ///< Slice of bytecounts_arena
  uint64_t jpeg_tables_off = 0;  ///< JPEG tables offset in file
  uint32_t jpeg_tables_len = 0;  ///< JPEG tables length in bytes

  // Lazy loading support
  LazyArrayInfo lazy_offsets;     ///< Deferred offsets metadata
  LazyArrayInfo lazy_bytecounts;  ///< Deferred bytecounts metadata

  // Cached JPEG tables (lazily loaded, valid for lifetime of mmap)
  mutable std::vector<uint8_t> jpeg_tables_cache;
  mutable std::atomic<JpegTablesState> jpeg_tables_state{
      JpegTablesState::kUninitialized};

  TilesRec() = default;

  // Custom move constructor for atomic member
  TilesRec(TilesRec&& other) noexcept
      : tile_w(other.tile_w),
        tile_h(other.tile_h),
        tiles_x(other.tiles_x),
        tiles_y(other.tiles_y),
        offsets(other.offsets),
        bytecounts(other.bytecounts),
        jpeg_tables_off(other.jpeg_tables_off),
        jpeg_tables_len(other.jpeg_tables_len),
        lazy_offsets(other.lazy_offsets),
        lazy_bytecounts(other.lazy_bytecounts),
        jpeg_tables_cache(std::move(other.jpeg_tables_cache)),
        jpeg_tables_state(other.jpeg_tables_state.load()) {}

  // Custom move assignment for atomic member
  TilesRec& operator=(TilesRec&& other) noexcept {
    if (this != &other) {
      tile_w = other.tile_w;
      tile_h = other.tile_h;
      tiles_x = other.tiles_x;
      tiles_y = other.tiles_y;
      offsets = other.offsets;
      bytecounts = other.bytecounts;
      jpeg_tables_off = other.jpeg_tables_off;
      jpeg_tables_len = other.jpeg_tables_len;
      lazy_offsets = other.lazy_offsets;
      lazy_bytecounts = other.lazy_bytecounts;
      jpeg_tables_cache = std::move(other.jpeg_tables_cache);
      jpeg_tables_state.store(other.jpeg_tables_state.load());
    }
    return *this;
  }

  // Delete copy constructor and assignment
  TilesRec(const TilesRec&) = delete;
  TilesRec& operator=(const TilesRec&) = delete;
};

/// Strip-based storage record
struct StripsRec {
  uint32_t rows_per_strip = 0;   ///< Rows per strip
  SpanU64 offsets;               ///< Slice of offsets_arena
  SpanU64 bytecounts;            ///< Slice of bytecounts_arena
  uint64_t jpeg_tables_off = 0;  ///< JPEG tables offset in file
  uint32_t jpeg_tables_len = 0;  ///< JPEG tables length in bytes

  // Lazy loading support
  LazyArrayInfo lazy_offsets;     ///< Deferred offsets metadata
  LazyArrayInfo lazy_bytecounts;  ///< Deferred bytecounts metadata

  // Cached JPEG tables (lazily loaded, valid for lifetime of mmap)
  mutable std::vector<uint8_t> jpeg_tables_cache;
  mutable std::atomic<JpegTablesState> jpeg_tables_state{
      JpegTablesState::kUninitialized};

  StripsRec() = default;

  // Custom move constructor for atomic member
  StripsRec(StripsRec&& other) noexcept
      : rows_per_strip(other.rows_per_strip),
        offsets(other.offsets),
        bytecounts(other.bytecounts),
        jpeg_tables_off(other.jpeg_tables_off),
        jpeg_tables_len(other.jpeg_tables_len),
        lazy_offsets(other.lazy_offsets),
        lazy_bytecounts(other.lazy_bytecounts),
        jpeg_tables_cache(std::move(other.jpeg_tables_cache)),
        jpeg_tables_state(other.jpeg_tables_state.load()) {}

  // Custom move assignment for atomic member
  StripsRec& operator=(StripsRec&& other) noexcept {
    if (this != &other) {
      rows_per_strip = other.rows_per_strip;
      offsets = other.offsets;
      bytecounts = other.bytecounts;
      jpeg_tables_off = other.jpeg_tables_off;
      jpeg_tables_len = other.jpeg_tables_len;
      lazy_offsets = other.lazy_offsets;
      lazy_bytecounts = other.lazy_bytecounts;
      jpeg_tables_cache = std::move(other.jpeg_tables_cache);
      jpeg_tables_state.store(other.jpeg_tables_state.load());
    }
    return *this;
  }

  // Delete copy constructor and assignment
  StripsRec(const StripsRec&) = delete;
  StripsRec& operator=(const StripsRec&) = delete;
};

/// Single embedded JPEG record
struct SingleJpegRec {
  uint64_t offset = 0;  ///< Offset in file
  uint64_t length = 0;  ///< Length in bytes
};

/// Page header containing common metadata for a TIFF page/IFD
struct PageHeader {
  uint32_t ifd_index = 0;          ///< Index of this IFD
  uint64_t ifd_offset = 0;         ///< Offset of IFD in file
  uint32_t width = 0;              ///< Image width
  uint32_t height = 0;             ///< Image height
  uint16_t samples_per_pixel = 0;  ///< Samples per pixel (e.g., 3 for RGB)
  uint16_t bits_per_sample = 8;    ///< Bits per sample (8, 16, 32, etc.)
  uint16_t photometric = 0;  ///< Photometric interpretation (2=RGB, 6=YCbCr)
  uint16_t compression = 0;  ///< Compression type (7=OJPEG, etc.)
  uint16_t predictor = 1;    ///< Predictor (1=none, 2=horizontal differencing)
  uint32_t new_subfile_type = 0;        ///< New subfile type tag
  Storage storage = Storage::kUnknown;  ///< Storage type
  uint32_t payload_id = 0;              ///< Index into appropriate pool
  std::string description;              ///< Image description (optional)
  std::optional<double> x_resolution;   ///< XResolution tag value
  std::optional<double> y_resolution;   ///< YResolution tag value
  std::optional<uint16_t>
      resolution_unit;  ///< ResolutionUnit (1=none, 2=inch, 3=cm)
};

/// Region of interest for reading
struct Roi {
  uint32_t x = 0;       ///< X offset
  uint32_t y = 0;       ///< Y offset
  uint32_t width = 0;   ///< Width
  uint32_t height = 0;  ///< Height
};

/// Main TIFF index structure with SoA design
///
/// This structure uses a Structure of Arrays design where:
/// - Hot data (page headers) are stored contiguously
/// - Cold data (tile/strip details) are in separate pools
/// - Large arrays (offsets, bytecounts) are in shared arenas
///
/// This minimizes allocations and maximizes cache locality.
///
/// The index is built by TiffParser and then becomes read-only.
/// Lazy loading of large arrays is thread-safe via internal locking.
class TiffIndex {
 public:
  TiffIndex() = default;
  ~TiffIndex();

  // Non-copyable, moveable
  TiffIndex(const TiffIndex&) = delete;
  TiffIndex& operator=(const TiffIndex&) = delete;
  TiffIndex(TiffIndex&&) noexcept;
  TiffIndex& operator=(TiffIndex&&) noexcept;

  // ========== Read-only accessors ==========

  /// Get file format information
  [[nodiscard]] bool IsBigTiff() const { return bigtiff_; }

  [[nodiscard]] bool IsLittleEndian() const { return little_endian_; }

  [[nodiscard]] uint64_t FileSize() const { return file_size_; }

  /// Get file descriptor for reading
  [[nodiscard]] int Fd() const { return fd_; }

  /// Get page headers
  [[nodiscard]] std::span<const PageHeader> Pages() const { return pages_; }

  [[nodiscard]] const PageHeader& Page(size_t idx) const { return pages_[idx]; }

  [[nodiscard]] size_t NumPages() const { return pages_.size(); }

  /// Get storage-specific payload pools (const access)
  [[nodiscard]] const TilesRec& Tiles(uint32_t payload_id) const {
    return tiles_pool_[payload_id];
  }

  [[nodiscard]] const StripsRec& Strips(uint32_t payload_id) const {
    return strips_pool_[payload_id];
  }

  [[nodiscard]] const SingleJpegRec& SingleJpeg(uint32_t payload_id) const {
    return single_pool_[payload_id];
  }

  /// Get a view of offsets for a given span
  ///
  /// @param s Span descriptor
  /// @return Const span view into the offsets arena
  [[nodiscard]] std::span<const uint64_t> Offsets(SpanU64 s) const {
    return {offsets_arena_.data() + s.start, s.count};
  }

  /// Get a view of bytecounts for a given span
  ///
  /// @param s Span descriptor
  /// @return Const span view into the bytecounts arena
  [[nodiscard]] std::span<const uint64_t> Bytecounts(SpanU64 s) const {
    return {bytecounts_arena_.data() + s.start, s.count};
  }

  // ========== Internal mutation (for TiffParser only) ==========
  // These should only be called during index construction

  /// Set file format information (builder pattern)
  void SetFormat(bool bigtiff, bool little_endian, uint64_t file_size) {
    bigtiff_ = bigtiff;
    little_endian_ = little_endian;
    file_size_ = file_size;
  }

  /// Set file descriptor (takes ownership of fd lifetime management)
  void SetFd(int file_descriptor) { fd_ = file_descriptor; }

  /// Reserve space for pages (optimization for known size)
  void ReservePages(size_t count) { pages_.reserve(count); }

  /// Add a page to the index
  void AddPage(PageHeader header) { pages_.push_back(std::move(header)); }

  /// Add a tiles record to the pool and return its index
  uint32_t AddTiles(TilesRec tiles) {
    uint32_t id = static_cast<uint32_t>(tiles_pool_.size());
    tiles_pool_.push_back(std::move(tiles));
    return id;
  }

  /// Add a strips record to the pool and return its index
  uint32_t AddStrips(StripsRec strips) {
    uint32_t id = static_cast<uint32_t>(strips_pool_.size());
    strips_pool_.push_back(std::move(strips));
    return id;
  }

  /// Add a single JPEG record to the pool and return its index
  uint32_t AddSingleJpeg(SingleJpegRec jpeg) {
    uint32_t id = static_cast<uint32_t>(single_pool_.size());
    single_pool_.push_back(std::move(jpeg));
    return id;
  }

  /// Append offsets to arena and return span descriptor
  SpanU64 AppendOffsets(std::span<const uint64_t> offsets) {
    uint32_t start = static_cast<uint32_t>(offsets_arena_.size());
    uint32_t count = static_cast<uint32_t>(offsets.size());
    offsets_arena_.insert(offsets_arena_.end(), offsets.begin(), offsets.end());
    return {start, count};
  }

  /// Append bytecounts to arena and return span descriptor
  SpanU64 AppendBytecounts(std::span<const uint64_t> bytecounts) {
    uint32_t start = static_cast<uint32_t>(bytecounts_arena_.size());
    uint32_t count = static_cast<uint32_t>(bytecounts.size());
    bytecounts_arena_.insert(bytecounts_arena_.end(), bytecounts.begin(),
                             bytecounts.end());
    return {start, count};
  }

  // ========== Lazy loading support (thread-safe const mutation) ==========
  // Internal use only - caller must hold mutex when mutating

  /// Access to arenas for lazy loading (requires holding LazyMutex())
  std::vector<uint64_t>& MutableOffsetsArena() const { return offsets_arena_; }

  std::vector<uint64_t>& MutableBytecountsArena() const {
    return bytecounts_arena_;
  }

  /// Access to pools for lazy loading (requires holding LazyMutex())
  std::vector<TilesRec>& MutableTilesPool() const { return tiles_pool_; }

  std::vector<StripsRec>& MutableStripsPool() const { return strips_pool_; }

  /// Get mutex for lazy loading synchronization
  std::mutex& LazyMutex() const { return lazy_load_mutex_; }

 private:
  /// File format information
  bool bigtiff_ = false;
  bool little_endian_ = true;
  uint64_t file_size_ = 0;

  /// File descriptor (owned by this class, closed in destructor)
  int fd_ = -1;

  /// Page headers (hot data - frequently accessed)
  std::vector<PageHeader> pages_;

  /// Storage-specific payload pools (cold data)
  /// Mutable to support lazy loading of deferred arrays
  mutable std::vector<TilesRec> tiles_pool_;
  mutable std::vector<StripsRec> strips_pool_;
  std::vector<SingleJpegRec> single_pool_;

  /// Shared arenas for all payloads (minimizes allocations)
  /// Mutable to allow lazy loading during const access
  mutable std::vector<uint64_t> offsets_arena_;
  mutable std::vector<uint64_t> bytecounts_arena_;

  /// Mutex for thread-safe lazy loading
  mutable std::mutex lazy_load_mutex_;
};

}  // namespace simpletiff

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INDEX_H_
