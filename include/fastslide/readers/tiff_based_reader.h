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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_READER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/tiff/tile_utilities.h"

/**
 * @file tiff_based_reader.h
 * @brief Base class for TIFF-based slide readers
 *
 * This header defines TiffBasedReader, the foundation for all slide readers
 * that work with TIFF files (Aperio SVS, QPTIFF, etc.). It provides common
 * functionality including:
 *
 * - TIFF file validation
 * - Thread-safe TIFF handle pool management
 * - Common TIFF operations (reading regions, tiles, strips)
 * - Associated image reading via libtiff
 * - Tile caching infrastructure
 *
 * **Usage:**
 * Format-specific readers (like AperioReader, QpTiffReader) inherit from
 * this class and implement format-specific logic for:
 * - Metadata parsing
 * - Pyramid structure discovery
 * - Tile organization and addressing
 * - Channel handling
 *
 * **Threading:**
 * The handle pool ensures thread-safe TIFF operations by providing one
 * TIFF handle per thread, preventing race conditions in libtiff.
 *
 * **Performance:**
 * For new TIFF operations, prefer the TiffFile RAII wrapper over manual
 * TIFFGetField calls. This provides better type safety, comprehensive
 * error handling, and eliminates manual resource management.
 *
 * @see TiffReaderFactory for the CRTP factory used to create readers
 * @see AperioReader for an Aperio SVS implementation
 * @see QpTiffReader for a PerkinElmer QPTIFF implementation
 */

namespace fs = std::filesystem;

namespace fastslide {

// Forward declaration for friend class
template <typename Derived>
class TiffReaderFactory;

/// @brief Base class for TIFF-based slide readers
///
/// This class provides common functionality for slide readers that work with
/// TIFF files, including file validation and common utility methods. It serves
/// as a foundation for format-specific readers like SVS and QPTIFF.
///
/// @note All readers now use simpletiff directly for TIFF operations, providing
/// better performance and thread safety without the need for handle pools.
class TiffBasedReader : public SlideReader {
 public:
  /// @brief Allow TiffReaderFactory to access protected methods
  template <typename Derived>
  friend class TiffReaderFactory;

  /// @brief Virtual destructor
  virtual ~TiffBasedReader() = default;

  /// @brief Get the filename of the slide
  /// @return Path to the slide file as string
  [[nodiscard]] std::string GetFilename() const { return filename_.string(); }

 protected:
  /// @brief Constructor for derived classes
  /// @param filename Path to the TIFF file
  explicit TiffBasedReader(fs::path filename);

  /// @brief Path to the TIFF file
  fs::path filename_;

  /// @brief Slide properties
  SlideProperties properties_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_BASED_READER_H_
