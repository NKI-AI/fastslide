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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READER_FACTORY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READER_FACTORY_H_

#include <filesystem>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"

/**
 * @file reader_factory.h
 * @brief Generic CRTP factory mixin for slide readers
 * 
 * This header defines ReaderFactory, a generic template class that consolidates
 * the common Create() flow shared by all slide readers (TIFF-based and
 * non-TIFF formats like MRXS).
 * 
 * This extends the pattern established by TiffReaderFactory to work with any
 * reader type, eliminating 40+ lines of boilerplate per reader while ensuring
 * consistent error handling conventions.
 * 
 * **Usage Pattern:**
 * ```cpp
 * class MrxsReader : public SlideReader, public ReaderFactory<MrxsReader> {
 *  public:
 *   static absl::StatusOr<std::unique_ptr<MrxsReader>> Create(
 *       std::filesystem::path filename) {
 *     return CreateImpl(filename);
 *   }
 *
 *  private:
 *   friend class ReaderFactory<MrxsReader>;  // Allow factory access
 *   
 *   // Hook 1: Validate input (static method)
 *   static absl::Status ValidateInput(const std::filesystem::path& filename);
 *   
 *   // Hook 2: Load metadata and construct reader
 *   //         This is format-specific and handled by CreateReaderImpl
 *   static absl::StatusOr<std::unique_ptr<MrxsReader>> CreateReaderImpl(
 *       const std::filesystem::path& filename);
 * };
 * ```
 * 
 * **Factory Flow:**
 * 1. Call `Derived::ValidateInput()` to check file format/existence
 * 2. Call `Derived::CreateReaderImpl()` to construct the fully-initialized reader
 * 3. Return the reader
 * 
 * **Derived Class Requirements:**
 * - Must declare `ReaderFactory<Derived>` as a friend
 * - Must implement `static absl::Status ValidateInput(const PathType& filename)`
 * - Must implement `static absl::StatusOr<std::unique_ptr<Derived>> CreateReaderImpl(const PathType& filename)`
 * 
 * All calls are resolved at compile time via `Derived::` calls, with zero
 * runtime overhead compared to hand-written factories.
 * 
 * @tparam Derived The concrete reader class (CRTP parameter)
 * 
 * @see TiffReaderFactory for a specialized version for TIFF-based readers
 * @see MrxsReader for an example of a non-TIFF reader using this factory
 */

namespace fs = std::filesystem;

namespace fastslide {

/// @brief Generic CRTP factory mixin for slide readers
///
/// This class consolidates the common Create() flow for all slide readers,
/// both TIFF-based and non-TIFF formats. It provides a simpler interface
/// than TiffReaderFactory that delegates most work to the derived class.
///
/// The pattern is:
/// 1. Validate input (format-specific hook)
/// 2. Create reader (format-specific hook that does all initialization)
/// 3. Return reader
///
/// @tparam Derived The concrete reader class (CRTP parameter)
template <typename Derived>
class ReaderFactory {
 protected:
  /// @brief Create a slide reader instance
  ///
  /// This template method implements a simplified creation flow for slide
  /// readers that don't fit the TIFF pattern:
  /// 1. Validate input (calls Derived::ValidateInput)
  /// 2. Create reader with all initialization (calls Derived::CreateReaderImpl)
  ///
  /// **Why this is simpler than TiffReaderFactory:**
  /// Non-TIFF formats often have more complex initialization that's tightly
  /// coupled with their data structures (e.g., MRXS needs Slidedat.ini data
  /// to construct the reader). Rather than forcing multiple hooks, we let the
  /// derived class handle construction however it needs.
  ///
  /// @tparam PathType The path type (auto-deduced from filename)
  /// @param filename Path to the slide file/directory
  /// @return StatusOr containing the reader instance or an error
  template <typename PathType>
  static absl::StatusOr<std::unique_ptr<Derived>> CreateImpl(
      PathType filename) {
    // Hook 1: Validate input (format-specific)
    RETURN_IF_ERROR(Derived::ValidateInput(filename),
                    "Failed to validate input");

    // Hook 2: Create reader with all initialization (format-specific)
    // The derived class handles: metadata loading, construction, property setup
    return Derived::CreateReaderImpl(filename);
  }

  /// @brief Protected constructor (only derived classes can instantiate via CRTP)
  ReaderFactory() = default;

  /// @brief Protected destructor (not polymorphic - no virtual needed)
  ~ReaderFactory() = default;

  /// @brief Delete copy constructor and assignment (CRTP mixin should not be copied)
  ReaderFactory(const ReaderFactory&) = delete;
  ReaderFactory& operator=(const ReaderFactory&) = delete;

  /// @brief Delete move constructor and assignment (CRTP mixin should not be moved)
  ReaderFactory(ReaderFactory&&) = delete;
  ReaderFactory& operator=(ReaderFactory&&) = delete;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READER_FACTORY_H_
