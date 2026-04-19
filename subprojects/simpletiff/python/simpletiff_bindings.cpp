// Copyright 2025 SimpleTIFF Authors

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_parser.h"

namespace py = pybind11;

namespace simpletiff::python {

/// Internal state for SimpleTiffReader
/// Manages file descriptor and TiffIndex lifecycle
struct ReaderState {
  int fd = -1;
  simpletiff::TiffIndex index;
  std::string file_path;

  ~ReaderState() {
    if (fd >= 0) {
      aifocore::portable_close(fd);
      fd = -1;
    }
  }

  // Non-copyable, non-moveable
  ReaderState() = default;
  ReaderState(const ReaderState&) = delete;
  ReaderState& operator=(const ReaderState&) = delete;
  ReaderState(ReaderState&&) = delete;
  ReaderState& operator=(ReaderState&&) = delete;
};

/// Forward declaration
class SimpleTiffReader;

/// Python wrapper for a TIFF page
class SimpleTiffPage {
 public:
  SimpleTiffPage(std::shared_ptr<ReaderState> state, uint32_t page_index)
      : state_(std::move(state)), page_index_(page_index) {
    if (!state_ || page_index_ >= state_->index.NumPages()) {
      throw std::runtime_error("Invalid page index");
    }
  }

  /// Get page width
  uint32_t width() const { return state_->index.Page(page_index_).width; }

  /// Get IFD index (order in which the parser enumerated pages)
  uint32_t ifd_index() const {
    return state_->index.Page(page_index_).ifd_index;
  }

  /// Get IFD offset in the TIFF file
  uint64_t ifd_offset() const {
    return state_->index.Page(page_index_).ifd_offset;
  }

  /// Get parent page index, if this page was referenced as a SubIFD
  std::optional<uint32_t> parent_page_index() const {
    return state_->index.Page(page_index_).parent_page_index;
  }

  /// Get indices of sub pages (SubIFDs) of this page
  std::vector<uint32_t> sub_page_indices() const {
    const auto children = state_->index.ChildPagesForPage(page_index_);
    return std::vector<uint32_t>(children.begin(), children.end());
  }

  /// Get number of sub pages (SubIFDs) of this page
  size_t num_sub_pages() const {
    return state_->index.ChildPagesForPage(page_index_).size();
  }

  /// Get sub pages (SubIFDs) of this page
  std::vector<SimpleTiffPage> sub_pages() const {
    const auto children = state_->index.ChildPagesForPage(page_index_);
    std::vector<SimpleTiffPage> pages;
    pages.reserve(children.size());
    for (const uint32_t idx : children) {
      pages.emplace_back(state_, idx);
    }
    return pages;
  }

  /// Get page height
  uint32_t height() const { return state_->index.Page(page_index_).height; }

  /// Get the raw NewSubfileType tag value (TIFF tag 254)
  uint32_t new_subfile_type() const {
    return state_->index.Page(page_index_).new_subfile_type;
  }

  /// True if this page is marked as reduced-resolution (pyramid/overview)
  bool is_reduced_resolution() const {
    // TIFF NewSubfileType bit 0: reduced-resolution image.
    return (new_subfile_type() & 0x1u) != 0;
  }

  /// Get samples per pixel (channels)
  uint16_t samples_per_pixel() const {
    return state_->index.Page(page_index_).samples_per_pixel;
  }

  /// Get bits per sample
  uint16_t bits_per_sample() const {
    return state_->index.Page(page_index_).bits_per_sample;
  }

  /// Get photometric interpretation
  uint16_t photometric() const {
    return state_->index.Page(page_index_).photometric;
  }

  /// Get compression type code
  uint16_t compression_code() const {
    return state_->index.Page(page_index_).compression;
  }

  /// Get compression type as enum (via Python)
  py::object compression() const {
    // Import the Compression enum from simpletiff module
    py::object simpletiff = py::module_::import("simpletiff");
    py::object compression_enum = simpletiff.attr("Compression");
    py::object from_code = compression_enum.attr("from_code");

    // Convert the compression code to enum
    return from_code(compression_code());
  }

  /// Get storage type as string
  std::string storage_type() const {
    switch (state_->index.Page(page_index_).storage) {
      case simpletiff::Storage::kTiles:
        return "tiled";
      case simpletiff::Storage::kStrips:
        return "striped";
      case simpletiff::Storage::kSingleJpeg:
        return "single_jpeg";
      default:
        return "unknown";
    }
  }

  /// Check if page is tiled
  bool is_tiled() const {
    return state_->index.Page(page_index_).storage ==
           simpletiff::Storage::kTiles;
  }

  /// Get tile width (only for tiled pages)
  uint16_t tile_width() const {
    if (!is_tiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tile_w;
  }

  /// Get tile height (only for tiled pages)
  uint16_t tile_height() const {
    if (!is_tiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tile_h;
  }

  /// Get number of tiles horizontally (only for tiled pages)
  uint32_t num_tiles_x() const {
    if (!is_tiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tiles_x;
  }

  /// Get number of tiles vertically (only for tiled pages)
  uint32_t num_tiles_y() const {
    if (!is_tiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tiles_y;
  }

  /// Get X resolution
  std::optional<double> x_resolution() const {
    return state_->index.Page(page_index_).x_resolution;
  }

  /// Get Y resolution
  std::optional<double> y_resolution() const {
    return state_->index.Page(page_index_).y_resolution;
  }

  /// Get resolution unit
  std::optional<uint16_t> resolution_unit() const {
    return state_->index.Page(page_index_).resolution_unit;
  }

  /// Get image description
  std::string description() const {
    return state_->index.Page(page_index_).description;
  }

  /// Read full page as numpy array
  py::array read() const {
    const auto& page = state_->index.Page(page_index_);
    simpletiff::Roi roi{0, 0, page.width, page.height};
    return read_region_internal(roi);
  }

  /// Read region from page
  /// @param location Tuple (x, y)
  /// @param size Tuple (width, height)
  py::array read_region(const std::tuple<uint32_t, uint32_t>& location,
                        const std::tuple<uint32_t, uint32_t>& size) const {
    auto [x, y] = location;
    auto [w, h] = size;
    simpletiff::Roi roi{x, y, w, h};
    return read_region_internal(roi);
  }

  /// Read a single tile by index (tiled pages only)
  py::array read_tile(uint32_t tile_index) const {
    if (!is_tiled()) {
      throw std::runtime_error("Page is not tiled");
    }

    std::vector<uint8_t> tile_data;
    int tile_w = 0, tile_h = 0;
    simpletiff::DecodeContext ctx;

    auto result = simpletiff::ReadTile(state_->index, page_index_, tile_index,
                                       ctx, tile_data, tile_w, tile_h);
    if (!result) {
      throw std::runtime_error("Failed to read tile: " +
                               result.error().message());
    }

    // Create numpy array based on bits_per_sample
    return create_numpy_array(tile_data, tile_h, tile_w, samples_per_pixel(),
                              bits_per_sample());
  }

 private:
  std::shared_ptr<ReaderState> state_;
  uint32_t page_index_;

  /// Internal method to read region
  py::array read_region_internal(const simpletiff::Roi& roi) const {
    const auto& page = state_->index.Page(page_index_);
    const uint32_t bps = page.bits_per_sample;
    const uint32_t spp = page.samples_per_pixel;
    const uint32_t bytes_per_sample = bps / 8;
    const uint32_t bytes_per_pixel = bytes_per_sample * spp;
    const size_t stride = roi.width * bytes_per_pixel;
    const size_t buffer_size = stride * roi.height;

    // Allocate buffer
    std::vector<uint8_t> buffer(buffer_size);

    // Read the region
    simpletiff::DecodeContext ctx;
    auto result = simpletiff::ReadPage(state_->index, page_index_, roi, ctx,
                                       buffer.data(), static_cast<int>(stride));
    if (!result) {
      throw std::runtime_error("Failed to read page region: " +
                               result.error().message());
    }

    // Create numpy array based on bits_per_sample
    return create_numpy_array(buffer, roi.height, roi.width, spp, bps);
  }

  /// Create numpy array with appropriate dtype
  static py::array create_numpy_array(const std::vector<uint8_t>& buffer,
                                      uint32_t height, uint32_t width,
                                      uint16_t spp, uint16_t bps) {
    std::vector<size_t> shape = {height, width, spp};

    if (bps == 8) {
      // uint8
      py::array_t<uint8_t> arr(shape);
      std::memcpy(arr.mutable_data(), buffer.data(), buffer.size());
      return arr;
    } else if (bps == 16) {
      // uint16
      py::array_t<uint16_t> arr(shape);
      std::memcpy(arr.mutable_data(), buffer.data(), buffer.size());
      return arr;
    } else if (bps == 32) {
      // uint32
      py::array_t<uint32_t> arr(shape);
      std::memcpy(arr.mutable_data(), buffer.data(), buffer.size());
      return arr;
    } else {
      throw std::runtime_error("Unsupported bits_per_sample: " +
                               std::to_string(bps));
    }
  }
};

/// Python wrapper for TIFF reader
class SimpleTiffReader {
 public:
  /// Factory method: create reader from file path
  static SimpleTiffReader from_file_path(const std::string& file_path) {
    auto state = std::make_shared<ReaderState>();
    state->file_path = file_path;

    // Open and parse the TIFF file
    if (!simpletiff::OpenTiff(file_path, state->index, state->fd)) {
      throw std::runtime_error("Failed to open TIFF file: " + file_path);
    }

    return SimpleTiffReader(std::move(state));
  }

  /// Get number of pages
  size_t num_pages() const { return state_->index.NumPages(); }

  /// Alias: number of IFDs (directories) parsed (includes SubIFDs)
  size_t num_ifds() const { return state_->index.NumPages(); }

  /// Number of root pages (i.e., pages without a parent SubIFD link)
  size_t num_root_pages() const { return root_page_indices().size(); }

  /// Check if this is a BigTIFF
  bool is_bigtiff() const { return state_->index.IsBigTiff(); }

  /// Get file size
  uint64_t file_size() const { return state_->index.FileSize(); }

  /// Get page by index
  SimpleTiffPage get_page(size_t index) const {
    if (index >= state_->index.NumPages()) {
      throw std::out_of_range("Page index out of range");
    }
    return SimpleTiffPage(state_, static_cast<uint32_t>(index));
  }

  /// Get page by index (for __getitem__)
  SimpleTiffPage getitem(ssize_t index) const {
    // Handle negative indexing
    if (index < 0) {
      index += static_cast<ssize_t>(state_->index.NumPages());
    }
    if (index < 0 || index >= static_cast<ssize_t>(state_->index.NumPages())) {
      throw std::out_of_range("Page index out of range");
    }
    return get_page(static_cast<size_t>(index));
  }

  /// Get length (for __len__)
  size_t len() const { return num_pages(); }

  /// Return indices of "root" pages (i.e., pages without a parent SubIFD link)
  std::vector<uint32_t> root_page_indices() const {
    std::vector<uint32_t> roots;
    roots.reserve(state_->index.NumPages());
    for (uint32_t i = 0; i < state_->index.NumPages(); ++i) {
      if (!state_->index.Page(i).parent_page_index.has_value()) {
        roots.push_back(i);
      }
    }
    return roots;
  }

  /// Return root pages (pages without a parent SubIFD link)
  std::vector<SimpleTiffPage> root_pages() const {
    auto roots = root_page_indices();
    std::vector<SimpleTiffPage> pages;
    pages.reserve(roots.size());
    for (const uint32_t idx : roots) {
      pages.emplace_back(state_, idx);
    }
    return pages;
  }

  /// Context manager enter
  SimpleTiffReader& enter() { return *this; }

  /// Context manager exit
  void exit(const py::object& exc_type, const py::object& exc_value,
            const py::object& traceback) {
    // Cleanup happens in destructor
    (void)exc_type;
    (void)exc_value;
    (void)traceback;
  }

 private:
  std::shared_ptr<ReaderState> state_;

  explicit SimpleTiffReader(std::shared_ptr<ReaderState> state)
      : state_(std::move(state)) {}
};

/// Iterator for SimpleTiffReader
class PageIterator {
 public:
  PageIterator(SimpleTiffReader reader, size_t index)
      : reader_(std::move(reader)), index_(index) {}

  SimpleTiffPage next() {
    if (index_ >= reader_.num_pages()) {
      throw py::stop_iteration();
    }
    return reader_.get_page(index_++);
  }

 private:
  SimpleTiffReader reader_;
  size_t index_;
};

}  // namespace simpletiff::python

/// Module definition
PYBIND11_MODULE(_simpletiff, m) {
  m.doc() = "SimpleTIFF: High-performance TIFF reader with Python bindings";

  using simpletiff::python::PageIterator;
  using simpletiff::python::SimpleTiffPage;
  using simpletiff::python::SimpleTiffReader;

  // SimpleTiffReader class
  py::class_<SimpleTiffReader>(m, "SimpleTiffReader")
      .def_static("from_file_path", &SimpleTiffReader::from_file_path,
                  "Create a SimpleTiffReader from a file path",
                  py::arg("file_path"))

      // Properties
      .def_property_readonly("num_pages", &SimpleTiffReader::num_pages,
                             "Number of pages in the TIFF file")
      .def_property_readonly(
          "num_ifds", &SimpleTiffReader::num_ifds,
          "Number of IFDs (directories) parsed, including SubIFDs")
      .def_property_readonly("num_root_pages",
                             &SimpleTiffReader::num_root_pages,
                             "Number of root pages (not SubIFDs)")
      .def_property_readonly("is_bigtiff", &SimpleTiffReader::is_bigtiff,
                             "True if this is a BigTIFF file")
      .def_property_readonly("file_size", &SimpleTiffReader::file_size,
                             "Size of the TIFF file in bytes")
      .def_property_readonly("root_page_indices",
                             &SimpleTiffReader::root_page_indices,
                             "Indices of root pages (not SubIFDs)")
      .def_property_readonly("root_pages", &SimpleTiffReader::root_pages,
                             "Root pages (not SubIFDs)")

      // Methods
      .def("get_page", &SimpleTiffReader::get_page,
           "Get a page by index (0-based)", py::arg("index"))

      // Special methods
      .def("__getitem__", &SimpleTiffReader::getitem,
           "Get a page by index (supports negative indexing)", py::arg("index"))
      .def("__len__", &SimpleTiffReader::len, "Get number of pages")
      .def(
          "__iter__",
          [](const SimpleTiffReader& self) { return PageIterator(self, 0); },
          py::keep_alive<0, 1>())

      // Context manager
      .def("__enter__", &SimpleTiffReader::enter,
           py::return_value_policy::reference_internal)
      .def("__exit__", &SimpleTiffReader::exit);

  // SimpleTiffPage class
  py::class_<SimpleTiffPage>(m, "SimpleTiffPage")
      // Properties
      .def_property_readonly("ifd_index", &SimpleTiffPage::ifd_index,
                             "IFD index (parser enumeration order)")
      .def_property_readonly("ifd_offset", &SimpleTiffPage::ifd_offset,
                             "IFD offset in the TIFF file")
      .def_property_readonly(
          "parent_page_index", &SimpleTiffPage::parent_page_index,
          "Parent page index if this page is a SubIFD, else None")
      .def_property_readonly("sub_page_indices",
                             &SimpleTiffPage::sub_page_indices,
                             "Indices of SubIFD child pages of this page")
      .def_property_readonly("num_sub_pages", &SimpleTiffPage::num_sub_pages,
                             "Number of SubIFD child pages of this page")
      .def_property_readonly("sub_pages", &SimpleTiffPage::sub_pages,
                             "SubIFD child pages of this page")
      .def_property_readonly("width", &SimpleTiffPage::width,
                             "Page width in pixels")
      .def_property_readonly("height", &SimpleTiffPage::height,
                             "Page height in pixels")
      .def_property_readonly(
          "new_subfile_type", &SimpleTiffPage::new_subfile_type,
          "NewSubfileType TIFF tag (254) as an integer bitmask")
      .def_property_readonly(
          "is_reduced_resolution", &SimpleTiffPage::is_reduced_resolution,
          "True if page is marked reduced-resolution (NewSubfileType bit 0)")
      .def_property_readonly("samples_per_pixel",
                             &SimpleTiffPage::samples_per_pixel,
                             "Number of samples per pixel (channels)")
      .def_property_readonly("bits_per_sample",
                             &SimpleTiffPage::bits_per_sample,
                             "Number of bits per sample")
      .def_property_readonly("photometric", &SimpleTiffPage::photometric,
                             "Photometric interpretation value")
      .def_property_readonly("compression", &SimpleTiffPage::compression,
                             "Compression type as Compression enum")
      .def_property_readonly("compression_code",
                             &SimpleTiffPage::compression_code,
                             "Compression type as numeric code")
      .def_property_readonly("storage_type", &SimpleTiffPage::storage_type,
                             "Storage type: 'tiled', 'striped', or "
                             "'single_jpeg'")
      .def_property_readonly("is_tiled", &SimpleTiffPage::is_tiled,
                             "True if page uses tiled storage")

      // Tiled-specific properties
      .def_property_readonly("tile_width", &SimpleTiffPage::tile_width,
                             "Tile width in pixels (only for tiled pages)")
      .def_property_readonly("tile_height", &SimpleTiffPage::tile_height,
                             "Tile height in pixels (only for tiled pages)")
      .def_property_readonly(
          "num_tiles_x", &SimpleTiffPage::num_tiles_x,
          "Number of tiles horizontally (only for tiled pages)")
      .def_property_readonly(
          "num_tiles_y", &SimpleTiffPage::num_tiles_y,
          "Number of tiles vertically (only for tiled pages)")

      // Resolution properties
      .def_property_readonly("x_resolution", &SimpleTiffPage::x_resolution,
                             "X resolution (pixels per unit, optional)")
      .def_property_readonly("y_resolution", &SimpleTiffPage::y_resolution,
                             "Y resolution (pixels per unit, optional)")
      .def_property_readonly("resolution_unit",
                             &SimpleTiffPage::resolution_unit,
                             "Resolution unit (1=none, 2=inch, 3=cm, optional)")

      // Description property
      .def_property_readonly("description", &SimpleTiffPage::description,
                             "Image description (optional TIFF tag)")

      // Methods
      .def(
          "read", &SimpleTiffPage::read,
          "Read the full page as a NumPy array\n\n"
          "Returns:\n"
          "    numpy.ndarray: Image data with shape (height, width, channels)\n"
          "                   dtype depends on bits_per_sample:\n"
          "                   - 8 bits: uint8\n"
          "                   - 16 bits: uint16\n"
          "                   - 32 bits: uint32")
      .def("read_region", &SimpleTiffPage::read_region,
           "Read a region from the page\n\n"
           "Args:\n"
           "    location: Tuple (x, y) - top-left corner of the region\n"
           "    size: Tuple (width, height) - size of the region\n\n"
           "Returns:\n"
           "    numpy.ndarray: Image data with shape (height, width, channels)",
           py::arg("location"), py::arg("size"))
      .def("read_tile", &SimpleTiffPage::read_tile,
           "Read a single tile by index (tiled pages only)\n\n"
           "Args:\n"
           "    tile_index: Linear tile index (row-major order)\n\n"
           "Returns:\n"
           "    numpy.ndarray: Tile data with shape (tile_height, tile_width, "
           "channels)",
           py::arg("tile_index"));

  // PageIterator class (internal)
  py::class_<PageIterator>(m, "_PageIterator")
      .def("__iter__", [](PageIterator& self) -> PageIterator& { return self; })
      .def("__next__", &PageIterator::next);

  // Module metadata
  m.attr("__version__") = "0.1.0";
}
