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
//
// Internal header exposing the CCITT (T.4 / T.6) state tables to the decoder.
// Not part of the public simpletiff API.
//
// The state machine encoding mirrors libtiff's tif_fax3.h (Sam Leffler / SGI),
// reproduced here so the auto-generated tables in ccitt_tables.cpp link cleanly
// without dragging in the libtiff TIFF runtime.

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INTERNAL_CCITT_TABLES_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INTERNAL_CCITT_TABLES_H_

#include <cstdint>

namespace simpletiff {
namespace ccitt {

// Finite-state-machine state codes used by the CCITT decoder. Values must
// match the codes baked into the auto-generated tables (do not renumber).
enum FaxState : uint8_t {
  S_Null = 0,
  S_Pass = 1,
  S_Horiz = 2,
  S_V0 = 3,
  S_VR = 4,
  S_VL = 5,
  S_Ext = 6,
  S_TermW = 7,
  S_TermB = 8,
  S_MakeUpW = 9,
  S_MakeUpB = 10,
  S_MakeUp = 11,
  S_EOL = 12,
};

/// One entry in a CCITT decoder state table.
///
/// Layout matches libtiff's TIFFFaxTabEnt verbatim because the auto-generated
/// state tables are reproduced byte-for-byte.
struct TIFFFaxTabEnt {
  unsigned char State;  ///< One of the FaxState enum values
  unsigned char Width;  ///< Width of the matched code in bits
  uint32_t Param;       ///< Run length / parameter associated with the state
};

extern const TIFFFaxTabEnt TIFFFaxMainTable[128];
extern const TIFFFaxTabEnt TIFFFaxWhiteTable[4096];
extern const TIFFFaxTabEnt TIFFFaxBlackTable[8192];

// Bit reversal table (MSB <-> LSB byte) for FillOrder=2 streams. Mirrors
// libtiff's TIFFBitRevTable.
extern const unsigned char kBitRevTable[256];
// Identity table for FillOrder=1 (MSB-first) streams.
extern const unsigned char kNoBitRevTable[256];

}  // namespace ccitt
}  // namespace simpletiff

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INTERNAL_CCITT_TABLES_H_
