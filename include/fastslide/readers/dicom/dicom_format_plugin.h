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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_FORMAT_PLUGIN_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_FORMAT_PLUGIN_H_

#include "fastslide/runtime/format_descriptor.h"

namespace fastslide {
namespace formats {
namespace dicom {

/// @brief Create a format descriptor for DICOM WSI files.
///
/// Registers the `.dcm` extension and provides a factory that constructs a
/// `DicomReader`.
///
/// @return FormatDescriptor for DICOM.
FormatDescriptor CreateDicomFormatDescriptor();

}  // namespace dicom
}  // namespace formats
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_DICOM_DICOM_FORMAT_PLUGIN_H_
