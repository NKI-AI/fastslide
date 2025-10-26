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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_READER_FACTORY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_READER_FACTORY_H_

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/readers/tiff_based_reader.h"

/**
 * @file tiff_reader_factory.h
 * @brief CRTP factory mixin for TIFF-based readers
 * 
 * This header defines the TiffReaderFactory template class that consolidates
 * the common Create() flow shared by TIFF-based readers (Aperio, QPTIFF, etc.).
 * 
 * The factory implements the Curiously Recurring Template Pattern (CRTP) to
 * provide compile-time polymorphism without virtual function overhead. It
 * eliminates boilerplate initialization code while allowing format-specific
 * customization through hook methods.
 * 
 * **Key Features:**
 * - Zero runtime overhead (all resolved at compile time)
 * - Type-safe factory pattern via CRTP
 * - Standardized initialization flow for all TIFF readers
 * - Format-specific hooks for metadata parsing
 * - Automatic error handling and validation
 * 
 * **Design Pattern:**
 * The factory follows a template method pattern with these steps:
 * 1. Allocate the concrete reader object
 * 2. Validate the TIFF file
 * 3. Initialize the thread-safe TIFF handle pool
 * 4. Call format-specific ProcessMetadata() hook
 * 5. Call format-specific PopulateSlideProperties() hook
 * 6. Return the fully initialized reader
 * 
 * @see TiffBasedReader for the base class all TIFF readers inherit from
 * @see AperioReader for an example of a reader using this factory
 * @see QpTiffReader for another example of a reader using this factory
 */

namespace fs = std::filesystem;

namespace fastslide {

/// @brief CRTP factory mixin for TIFF-based readers
///
/// This class consolidates the common Create() flow shared by TIFF-based
/// readers (Aperio, QPTIFF, etc.). It implements the factory pattern using
/// the Curiously Recurring Template Pattern (CRTP) to enable compile-time
/// polymorphism without virtual function overhead.
///
/// **Usage Pattern:**
/// ```cpp
/// class MyReader : public TiffBasedReader,
///                  public TiffReaderFactory<MyReader> {
///  public:
///   static absl::StatusOr<std::unique_ptr<MyReader>> Create(
///       std::string_view filename) {
///     return CreateImpl(filename);
///   }
///
///  private:
///   friend class TiffReaderFactory<MyReader>;  // Allow factory access
///   explicit MyReader(std::string_view filename);
///   absl::Status ProcessMetadata();
///   void PopulateSlideProperties();
/// };
/// ```
///
/// **Derived Class Requirements:**
/// - Must inherit from TiffBasedReader (for ValidateTiffFile, InitializeHandlePool)
/// - Must declare TiffReaderFactory<Derived> as a friend
/// - Must have a private constructor accepting filename (string_view or fs::path)
/// - Must implement `absl::Status ProcessMetadata()`
/// - Must implement `void PopulateSlideProperties()`
///
/// **Factory Flow:**
/// 1. Allocate the concrete reader object
/// 2. Validate the TIFF file before opening tiles
/// 3. Initialize the thread-safe TIFF handle pool
/// 4. Call format-specific metadata parsing (ProcessMetadata hook)
/// 5. Convert metadata to common SlideProperties view (PopulateSlideProperties hook)
/// 6. Return the fully initialized reader
///
/// All calls are resolved at compile time via `static_cast<Derived*>`,
/// with zero runtime overhead compared to hand-written factories.
///
/// @tparam Derived The concrete reader class (CRTP parameter)
template <typename Derived>
class TiffReaderFactory {
 protected:
  /// @brief Create a TIFF-based reader instance
  ///
  /// This template method implements the common creation flow for all
  /// TIFF-based readers:
  /// 1. Allocate the derived reader using its private constructor
  /// 2. Validate the TIFF file
  /// 3. Initialize the TIFF handle pool
  /// 4. Process format-specific metadata (calls Derived::ProcessMetadata)
  /// 5. Populate common slide properties (calls Derived::PopulateSlideProperties)
  ///
  /// @tparam PathType The path type (auto-deduced from filename)
  /// @param filename Path to the TIFF file (string_view or fs::path)
  /// @return StatusOr containing the reader instance or an error
  template <typename PathType>
  static absl::StatusOr<std::unique_ptr<Derived>> CreateImpl(
      PathType filename) {
    // Allocate the concrete reader object
    // Use unique_ptr with new to call private constructor
    auto reader = std::unique_ptr<Derived>(new Derived(filename));

    // Get the underlying TiffBasedReader to access protected methods
    TiffBasedReader* base = static_cast<TiffBasedReader*>(reader.get());

    // Validate TIFF file before opening tiles
    // Convert PathType to fs::path for ValidateTiffFile
    fs::path file_path;
    if constexpr (std::is_same_v<PathType, std::string_view>) {
      file_path = fs::path(std::string(filename));
    } else {
      file_path = filename;
    }
    RETURN_IF_ERROR(TiffBasedReader::ValidateTiffFile(file_path),
                    "Failed to validate TIFF file");

    // Initialize thread-safe TIFF handle pool
    RETURN_IF_ERROR(base->InitializeHandlePool(),
                    "Failed to initialize handle pool");

    // Call format-specific metadata parsing
    RETURN_IF_ERROR(reader->ProcessMetadata(), "Failed to process metadata");

    // Convert metadata to common SlideProperties view
    reader->PopulateSlideProperties();

    return reader;
  }

  /// @brief Protected constructor (only derived classes can instantiate via CRTP)
  TiffReaderFactory() = default;

  /// @brief Protected destructor (not polymorphic - no virtual needed)
  ~TiffReaderFactory() = default;

  /// @brief Delete copy constructor and assignment (CRTP mixin should not be copied)
  TiffReaderFactory(const TiffReaderFactory&) = delete;
  TiffReaderFactory& operator=(const TiffReaderFactory&) = delete;

  /// @brief Delete move constructor and assignment (CRTP mixin should not be moved)
  TiffReaderFactory(TiffReaderFactory&&) = delete;
  TiffReaderFactory& operator=(TiffReaderFactory&&) = delete;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TIFF_READER_FACTORY_H_
