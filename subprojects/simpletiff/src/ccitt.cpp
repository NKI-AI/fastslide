// =============================================================================
// CCITT Group 3 (T.4) and Group 4 (T.6) bilevel decompression.
//
// This file is derived from libtiff's tif_fax3.c / tif_fax3.h, originally:
//
//     Copyright (c) 1990-1997 Sam Leffler
//     Copyright (c) 1991-1997 Silicon Graphics, Inc.
//
//     Permission to use, copy, modify, distribute, and sell this software and
//     its documentation for any purpose is hereby granted without fee,
//     provided that (i) the above copyright notices and this permission
//     notice appear in all copies of the software and related documentation,
//     and (ii) the names of Sam Leffler and Silicon Graphics may not be used
//     in any advertising or publicity relating to the software without the
//     specific, prior written permission of Sam Leffler and Silicon Graphics.
//
//     THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
//     EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
//     WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
//     Decoder support is derived, with permission, from the code in Frank
//     Cringle's viewfax program; Copyright (C) 1990, 1995  Frank D. Cringle.
//
// Modifications by Jonas Teuwen, 2026: extracted the decoder from libtiff's
// TIFF runtime, replaced TIFF*/Fax3CodecState with a self-contained C++ state
// struct and std::span/std::vector buffers, dropped the encoder, and exposed
// the surface as `simpletiff::DecompressCcittG4` / `DecompressCcittG3`.
// Distributed under the Apache License, Version 2.0 alongside the original
// BSD-style license above.
// =============================================================================

#include "simpletiff/ccitt.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "simpletiff/internal/ccitt_tables.h"

namespace simpletiff {

using ::aifocore::Result;
using ::aifocore::StatusCode;

namespace {

using ccitt::kBitRevTable;
using ccitt::kNoBitRevTable;
using ccitt::S_EOL;
using ccitt::S_Ext;
using ccitt::S_Horiz;
using ccitt::S_MakeUp;
using ccitt::S_MakeUpB;
using ccitt::S_MakeUpW;
using ccitt::S_Pass;
using ccitt::S_TermB;
using ccitt::S_TermW;
using ccitt::S_V0;
using ccitt::S_VL;
using ccitt::S_VR;
using ccitt::TIFFFaxBlackTable;
using ccitt::TIFFFaxMainTable;
using ccitt::TIFFFaxTabEnt;
using ccitt::TIFFFaxWhiteTable;

// Mirrors libtiff's Fax3CodecState fields used by the decoder. We only retain
// the subset required for G3/G4 decode (no encoder, no fax-mode flags beyond
// "this stream lacks EOL markers").
struct DecodeState {
  std::vector<uint32_t> runs_storage;  // backing storage for cur/refruns
  uint32_t nruns = 0;                  // size (in elements) of one run array
  uint32_t* curruns = nullptr;
  uint32_t* refruns = nullptr;
  uint32_t rowpixels = 0;
  uint32_t rowbytes = 0;
  // Counters mirroring libtiff's bail-out heuristics.
  int eof_reached_count = 0;
  int eol_reached_count = 0;
  int unexpected_reached_count = 0;
  // Set when a G3 stream is detected to have no EOL markers; matches
  // libtiff's FAXMODE_NOEOL flag semantics.
  bool no_eol = false;
  int line = 0;
};

// Arbitrary threshold (matches libtiff EOF_REACHED_COUNT_THRESHOLD) to avoid
// pathologically corrupted strips causing apparently endless loops.
constexpr int kRecoverableErrorThreshold = 8192;

// Round up `value` to the next multiple of `multiple` (must be a power of two).
constexpr uint32_t RoundUp32(uint32_t value, uint32_t multiple) {
  return (value + (multiple - 1U)) & ~(multiple - 1U);
}

// Allocate the run arrays for a given row width. Mirrors Fax3SetupState's
// nruns computation: 2 * roundup32(rowpixels + 1, 32) for 2-D codecs.
bool AllocateRuns(DecodeState* sp, uint32_t rowpixels, bool needs_ref_line) {
  const uint64_t base = RoundUp32(rowpixels + 1U, 32U);
  if (base == 0 || base > 0x3FFFFFFFULL) {
    return false;
  }
  uint64_t per_row = base;
  if (needs_ref_line) {
    per_row *= 2U;
  }
  if (per_row > 0x3FFFFFFFULL) {
    return false;
  }
  const uint64_t total = per_row * 2ULL;  // cur + ref
  if (total > 0x7FFFFFFFULL) {
    return false;
  }
  sp->nruns = static_cast<uint32_t>(per_row);
  sp->runs_storage.assign(static_cast<size_t>(total), 0U);
  sp->curruns = sp->runs_storage.data();
  sp->refruns = needs_ref_line ? (sp->curruns + sp->nruns) : nullptr;
  sp->rowpixels = rowpixels;
  sp->rowbytes = (rowpixels + 7U) / 8U;
  return true;
}

// Bit-fill a row according to the white/black runs generated during decoding.
// Direct port of `_TIFFFax3fillruns` with the int64-aligned fast path
// reduced to a memset (we measured negligible difference for typical WSI
// mask widths and prefer the simpler form).
void FaxFillRuns(unsigned char* buf, uint32_t* runs, uint32_t* erun,
                 uint32_t lastx) {
  static const unsigned char kFillMasks[] = {0x00, 0x80, 0xc0, 0xe0, 0xf0,
                                             0xf8, 0xfc, 0xfe, 0xff};
  uint32_t x = 0;
  if ((erun - runs) & 1) {
    *erun++ = 0;
  }
  for (; runs < erun; runs += 2) {
    uint32_t run = runs[0];
    if (x + run > lastx || run > lastx) {
      run = runs[0] = (lastx - x);
    }
    if (run) {
      unsigned char* cp = buf + (x >> 3);
      uint32_t bx = x & 7U;
      if (run > 8U - bx) {
        if (bx) {
          *cp++ &= 0xff << (8U - bx);
          run -= 8U - bx;
        }
        const int32_t n = static_cast<int32_t>(run >> 3);
        if (n != 0) {
          std::memset(cp, 0, static_cast<size_t>(n));
          cp += n;
          run &= 7U;
        }
        if (run) {
          cp[0] &= 0xff >> run;
        }
      } else {
        cp[0] &= static_cast<unsigned char>(~(kFillMasks[run] >> bx));
      }
      x += runs[0];
    }
    run = runs[1];
    if (x + run > lastx || run > lastx) {
      run = runs[1] = lastx - x;
    }
    if (run) {
      unsigned char* cp = buf + (x >> 3);
      uint32_t bx = x & 7U;
      if (run > 8U - bx) {
        if (bx) {
          *cp++ |= 0xff >> bx;
          run -= 8U - bx;
        }
        const int32_t n = static_cast<int32_t>(run >> 3);
        if (n != 0) {
          std::memset(cp, 0xff, static_cast<size_t>(n));
          cp += n;
          run &= 7U;
        }
        if (run) {
          cp[0] = static_cast<unsigned char>((cp[0] | (0xff00 >> run)) & 0xff);
        }
      } else {
        cp[0] |= kFillMasks[run] >> bx;
      }
      x += runs[1];
    }
  }
}

// =============================================================================
// Bit-stream macros (ported verbatim from libtiff tif_fax3.h).
//
// These rely on the following local variables/state existing in the calling
// scope:
//   - cp        : current byte pointer in the input stream
//   - ep        : end-of-input pointer
//   - bitmap    : 256-byte bit-reversal lookup (or identity)
//   - BitAcc    : bit accumulator (uint32_t)
//   - BitsAvail : number of valid bits in BitAcc (int)
//   - EOLcnt    : count of EOL codes recognized (int)
//   - TabEnt    : pointer to the most recently looked-up table entry
// =============================================================================
#define EndOfData() (cp >= ep)
#define NeedBits8(n, eoflab)                              \
  do {                                                    \
    if (BitsAvail < (n)) {                                \
      if (EndOfData()) {                                  \
        if (BitsAvail == 0)                               \
          goto eoflab;                                    \
        BitsAvail = (n);                                  \
      } else {                                            \
        BitAcc |= ((uint32_t)bitmap[*cp++]) << BitsAvail; \
        BitsAvail += 8;                                   \
      }                                                   \
    }                                                     \
  } while (0)
#define NeedBits16(n, eoflab)                                 \
  do {                                                        \
    if (BitsAvail < (n)) {                                    \
      if (EndOfData()) {                                      \
        if (BitsAvail == 0)                                   \
          goto eoflab;                                        \
        BitsAvail = (n);                                      \
      } else {                                                \
        BitAcc |= ((uint32_t)bitmap[*cp++]) << BitsAvail;     \
        if ((BitsAvail += 8) < (n)) {                         \
          if (EndOfData()) {                                  \
            BitsAvail = (n);                                  \
          } else {                                            \
            BitAcc |= ((uint32_t)bitmap[*cp++]) << BitsAvail; \
            BitsAvail += 8;                                   \
          }                                                   \
        }                                                     \
      }                                                       \
    }                                                         \
  } while (0)
#define GetBits(n) (BitAcc & ((1U << (n)) - 1U))
#define ClrBits(n)    \
  do {                \
    BitsAvail -= (n); \
    BitAcc >>= (n);   \
  } while (0)

// Error-reporting macros expected by EXPAND1D/EXPAND2D. Decoded into counter
// bumps; the decoder falls back to best-effort row recovery as in libtiff.
#define unexpected(table, a0_val)   \
  do {                              \
    (void)(table);                  \
    (void)(a0_val);                 \
    ++sp->unexpected_reached_count; \
  } while (0)
#define extension(a0_val)           \
  do {                              \
    (void)(a0_val);                 \
    ++sp->unexpected_reached_count; \
  } while (0)
#define badlength(a0_val, lastx_val) \
  do {                               \
    (void)(a0_val);                  \
    (void)(lastx_val);               \
    ++sp->eol_reached_count;         \
  } while (0)
#define prematureEOF(a0_val) \
  do {                       \
    (void)(a0_val);          \
    ++sp->eof_reached_count; \
  } while (0)
#define tryG3WithoutEOL(a0_val) (void)(a0_val)

// SETVALUE / CHECK_b1 / CLEANUP_RUNS / SYNC_EOL / EXPAND1D / EXPAND2D ported
// verbatim from libtiff with TIFFErrorExtR replaced by `return false`.
#define SETVALUE(x)                                          \
  do {                                                       \
    if (pa >= thisrun + sp->nruns) {                         \
      return false;                                          \
    }                                                        \
    *pa++ = (uint32_t)((uint32_t)RunLength + (uint32_t)(x)); \
    a0 += (int)(uint32_t)(x);                                \
    RunLength = 0;                                           \
  } while (0)

#define SYNC_EOL(eoflab, retrywithouteol) \
  do {                                    \
    if (!sp->no_eol) {                    \
      if (EOLcnt == 0) {                  \
        for (;;) {                        \
          NeedBits16(11, eoflab);         \
          if (GetBits(11) == 0)           \
            break;                        \
          ClrBits(1);                     \
        }                                 \
      }                                   \
      for (;;) {                          \
        NeedBits8(8, noEOLFound);         \
        if (GetBits(8))                   \
          break;                          \
        ClrBits(8);                       \
      }                                   \
      while (GetBits(1) == 0)             \
        ClrBits(1);                       \
      ClrBits(1);                         \
      EOLcnt = 0;                         \
      break;                              \
    noEOLFound:                           \
      sp->no_eol = true;                  \
      tryG3WithoutEOL(a0);                \
      goto retrywithouteol;               \
    }                                     \
  } while (0)

#define CLEANUP_RUNS()                         \
  do {                                         \
    if (RunLength)                             \
      SETVALUE(0);                             \
    if (a0 != (int)lastx) {                    \
      badlength(a0, lastx);                    \
      while (a0 > (int)lastx && pa > thisrun)  \
        a0 -= (int)*--pa;                      \
      if (a0 < (int)lastx) {                   \
        if (a0 < 0)                            \
          a0 = 0;                              \
        if ((pa - thisrun) & 1)                \
          SETVALUE(0);                         \
        SETVALUE((uint32_t)((int)lastx - a0)); \
      } else if (a0 > (int)lastx) {            \
        SETVALUE(lastx);                       \
        SETVALUE(0);                           \
      }                                        \
    }                                          \
  } while (0)

#define LOOKUP8(wid, tab, eoflab)  \
  do {                             \
    NeedBits8(wid, eoflab);        \
    TabEnt = (tab) + GetBits(wid); \
    ClrBits(TabEnt->Width);        \
  } while (0)
#define LOOKUP16(wid, tab, eoflab) \
  do {                             \
    NeedBits16(wid, eoflab);       \
    TabEnt = (tab) + GetBits(wid); \
    ClrBits(TabEnt->Width);        \
  } while (0)

#define EXPAND1D(eoflab)                                            \
  do {                                                              \
    for (;;) {                                                      \
      for (;;) {                                                    \
        LOOKUP16(12, TIFFFaxWhiteTable, eof1d);                     \
        switch (TabEnt->State) {                                    \
          case S_EOL:                                               \
            EOLcnt = 1;                                             \
            goto done1d;                                            \
          case S_TermW:                                             \
            SETVALUE(TabEnt->Param);                                \
            goto doneWhite1d;                                       \
          case S_MakeUpW:                                           \
          case S_MakeUp:                                            \
            a0 = (int)((uint32_t)a0 + TabEnt->Param);               \
            RunLength = (int)((uint32_t)RunLength + TabEnt->Param); \
            break;                                                  \
          default:                                                  \
            unexpected("WhiteTable", a0);                           \
            goto done1d;                                            \
        }                                                           \
      }                                                             \
    doneWhite1d:                                                    \
      if (a0 >= (int)lastx)                                         \
        goto done1d;                                                \
      for (;;) {                                                    \
        LOOKUP16(13, TIFFFaxBlackTable, eof1d);                     \
        switch (TabEnt->State) {                                    \
          case S_EOL:                                               \
            EOLcnt = 1;                                             \
            goto done1d;                                            \
          case S_TermB:                                             \
            SETVALUE(TabEnt->Param);                                \
            goto doneBlack1d;                                       \
          case S_MakeUpB:                                           \
          case S_MakeUp:                                            \
            a0 += (int)TabEnt->Param;                               \
            RunLength += (int)TabEnt->Param;                        \
            break;                                                  \
          default:                                                  \
            unexpected("BlackTable", a0);                           \
            goto done1d;                                            \
        }                                                           \
      }                                                             \
    doneBlack1d:                                                    \
      if (a0 >= (int)lastx)                                         \
        goto done1d;                                                \
      if (*(pa - 1) == 0 && *(pa - 2) == 0)                         \
        pa -= 2;                                                    \
    }                                                               \
  eof1d:                                                            \
    prematureEOF(a0);                                               \
    CLEANUP_RUNS();                                                 \
    goto eoflab;                                                    \
  done1d:                                                           \
    CLEANUP_RUNS();                                                 \
  } while (0)

#define CHECK_b1                               \
  do {                                         \
    if (pa != thisrun)                         \
      while (b1 <= a0 && b1 < (int)lastx) {    \
        if (pb + 1 >= sp->refruns + sp->nruns) \
          return false;                        \
        b1 += (int)(pb[0] + pb[1]);            \
        pb += 2;                               \
      }                                        \
  } while (0)

#define EXPAND2D(eoflab)                                                  \
  do {                                                                    \
    while (a0 < (int)lastx) {                                             \
      if (pa >= thisrun + sp->nruns)                                      \
        return false;                                                     \
      LOOKUP8(7, TIFFFaxMainTable, eof2d);                                \
      switch (TabEnt->State) {                                            \
        case S_Pass:                                                      \
          CHECK_b1;                                                       \
          if (pb + 1 >= sp->refruns + sp->nruns)                          \
            return false;                                                 \
          b1 = b1 + (int)*pb++;                                           \
          RunLength = (int)((uint32_t)RunLength + (uint32_t)(b1 - a0));   \
          a0 = b1;                                                        \
          b1 = b1 + (int)*pb++;                                           \
          break;                                                          \
        case S_Horiz:                                                     \
          if ((pa - thisrun) & 1) {                                       \
            for (;;) {                                                    \
              LOOKUP16(13, TIFFFaxBlackTable, eof2d);                     \
              switch (TabEnt->State) {                                    \
                case S_TermB:                                             \
                  SETVALUE(TabEnt->Param);                                \
                  goto doneWhite2da;                                      \
                case S_MakeUpB:                                           \
                case S_MakeUp:                                            \
                  a0 = (int)((uint32_t)a0 + TabEnt->Param);               \
                  RunLength = (int)((uint32_t)RunLength + TabEnt->Param); \
                  break;                                                  \
                default:                                                  \
                  goto badBlack2d;                                        \
              }                                                           \
            }                                                             \
          doneWhite2da:;                                                  \
            for (;;) {                                                    \
              LOOKUP16(12, TIFFFaxWhiteTable, eof2d);                     \
              switch (TabEnt->State) {                                    \
                case S_TermW:                                             \
                  SETVALUE(TabEnt->Param);                                \
                  goto doneBlack2da;                                      \
                case S_MakeUpW:                                           \
                case S_MakeUp:                                            \
                  a0 = (int)((uint32_t)a0 + TabEnt->Param);               \
                  RunLength = (int)((uint32_t)RunLength + TabEnt->Param); \
                  break;                                                  \
                default:                                                  \
                  goto badWhite2d;                                        \
              }                                                           \
            }                                                             \
          doneBlack2da:;                                                  \
          } else {                                                        \
            for (;;) {                                                    \
              LOOKUP16(12, TIFFFaxWhiteTable, eof2d);                     \
              switch (TabEnt->State) {                                    \
                case S_TermW:                                             \
                  SETVALUE(TabEnt->Param);                                \
                  goto doneWhite2db;                                      \
                case S_MakeUpW:                                           \
                case S_MakeUp:                                            \
                  a0 = (int)((uint32_t)a0 + TabEnt->Param);               \
                  RunLength = (int)((uint32_t)RunLength + TabEnt->Param); \
                  break;                                                  \
                default:                                                  \
                  goto badWhite2d;                                        \
              }                                                           \
            }                                                             \
          doneWhite2db:;                                                  \
            for (;;) {                                                    \
              LOOKUP16(13, TIFFFaxBlackTable, eof2d);                     \
              switch (TabEnt->State) {                                    \
                case S_TermB:                                             \
                  SETVALUE(TabEnt->Param);                                \
                  goto doneBlack2db;                                      \
                case S_MakeUpB:                                           \
                case S_MakeUp:                                            \
                  a0 = (int)((uint32_t)a0 + TabEnt->Param);               \
                  RunLength = (int)((uint32_t)RunLength + TabEnt->Param); \
                  break;                                                  \
                default:                                                  \
                  goto badBlack2d;                                        \
              }                                                           \
            }                                                             \
          doneBlack2db:;                                                  \
          }                                                               \
          CHECK_b1;                                                       \
          break;                                                          \
        case S_V0:                                                        \
          CHECK_b1;                                                       \
          SETVALUE(b1 - a0);                                              \
          if (pb >= sp->refruns + sp->nruns)                              \
            return false;                                                 \
          b1 = b1 + (int)*pb++;                                           \
          break;                                                          \
        case S_VR:                                                        \
          CHECK_b1;                                                       \
          SETVALUE((int)((uint32_t)(b1 - a0) + TabEnt->Param));           \
          if (pb >= sp->refruns + sp->nruns)                              \
            return false;                                                 \
          b1 = b1 + (int)*pb++;                                           \
          break;                                                          \
        case S_VL:                                                        \
          CHECK_b1;                                                       \
          if (b1 < (int)((uint32_t)a0 + TabEnt->Param)) {                 \
            unexpected("VL", a0);                                         \
            goto eol2d;                                                   \
          }                                                               \
          SETVALUE((int)((uint32_t)(b1 - a0) - TabEnt->Param));           \
          b1 = b1 - (int)*--pb;                                           \
          break;                                                          \
        case S_Ext:                                                       \
          *pa++ = (uint32_t)((int)lastx - a0);                            \
          extension(a0);                                                  \
          goto eol2d;                                                     \
        case S_EOL:                                                       \
          *pa++ = (uint32_t)((int)lastx - a0);                            \
          NeedBits8(4, eof2d);                                            \
          if (GetBits(4))                                                 \
            unexpected("EOL", a0);                                        \
          ClrBits(4);                                                     \
          EOLcnt = 1;                                                     \
          goto eol2d;                                                     \
        default:                                                          \
        badMain2d:                                                        \
          unexpected("MainTable", a0);                                    \
          goto eol2d;                                                     \
        badBlack2d:                                                       \
          unexpected("BlackTable", a0);                                   \
          goto eol2d;                                                     \
        badWhite2d:                                                       \
          unexpected("WhiteTable", a0);                                   \
          goto eol2d;                                                     \
        eof2d:                                                            \
          prematureEOF(a0);                                               \
          CLEANUP_RUNS();                                                 \
          goto eoflab;                                                    \
      }                                                                   \
    }                                                                     \
    if (RunLength) {                                                      \
      if (RunLength + a0 < (int)lastx) {                                  \
        NeedBits8(1, eof2d);                                              \
        if (!GetBits(1))                                                  \
          goto badMain2d;                                                 \
        ClrBits(1);                                                       \
      }                                                                   \
      SETVALUE(0);                                                        \
    }                                                                     \
  eol2d:                                                                  \
    CLEANUP_RUNS();                                                       \
  } while (0)

bool RecoverableThresholdExceeded(const DecodeState* sp) {
  return sp->eof_reached_count >= kRecoverableErrorThreshold ||
         sp->eol_reached_count >= kRecoverableErrorThreshold ||
         sp->unexpected_reached_count >= kRecoverableErrorThreshold;
}

bool DecodeG4Rows(DecodeState* sp, std::span<const uint8_t> compressed,
                  const unsigned char* bitmap, uint32_t height,
                  std::vector<uint8_t>& out) {
  out.assign(static_cast<size_t>(height) * sp->rowbytes, 0xFFU);
  uint8_t* buf = out.data();
  uint32_t occ = static_cast<uint32_t>(out.size());

  uint32_t BitAcc = 0;
  int BitsAvail = 0;
  int EOLcnt = 0;
  const uint8_t* cp = compressed.data();
  const uint8_t* ep = cp + compressed.size();

  // Per-row scratch.
  int a0 = 0;
  int b1 = 0;
  int RunLength = 0;
  uint32_t* pa = nullptr;
  uint32_t* thisrun = nullptr;
  uint32_t* pb = nullptr;
  const TIFFFaxTabEnt* TabEnt = nullptr;
  const int lastx = static_cast<int>(sp->rowpixels);

  while (occ > 0) {
    if (RecoverableThresholdExceeded(sp))
      return false;
    a0 = 0;
    RunLength = 0;
    pa = thisrun = sp->curruns;
    pb = sp->refruns;
    b1 = static_cast<int>(*pb++);
    EXPAND2D(EOFG4);
    if (EOLcnt)
      goto eofg4;
    if (((lastx + 7) >> 3) > static_cast<int>(occ))
      return false;
    FaxFillRuns(buf, thisrun, pa, static_cast<uint32_t>(lastx));
    SETVALUE(0);  // imaginary change for reference
    std::swap(sp->curruns, sp->refruns);
    buf += sp->rowbytes;
    occ -= sp->rowbytes;
    sp->line++;
    continue;
  EOFG4:
    NeedBits16(13, badg4);
  badg4:
    ClrBits(13);
  eofg4:
    if (((lastx + 7) >> 3) > static_cast<int>(occ))
      return false;
    FaxFillRuns(buf, thisrun, pa, static_cast<uint32_t>(lastx));
    return sp->line != 0;
  }
  return true;
}

bool DecodeG3Rows(DecodeState* sp, std::span<const uint8_t> compressed,
                  const unsigned char* bitmap, uint32_t height,
                  std::vector<uint8_t>& out) {
  out.assign(static_cast<size_t>(height) * sp->rowbytes, 0xFFU);
  uint8_t* buf = out.data();
  uint32_t occ = static_cast<uint32_t>(out.size());

  uint32_t BitAcc = 0;
  int BitsAvail = 0;
  int EOLcnt = 0;
  const uint8_t* cp = compressed.data();
  const uint8_t* ep = cp + compressed.size();

  int a0 = 0;
  int RunLength = 0;
  uint32_t* pa = nullptr;
  uint32_t* thisrun = nullptr;
  const TIFFFaxTabEnt* TabEnt = nullptr;
  const int lastx = static_cast<int>(sp->rowpixels);

retry_without_eol:
  thisrun = sp->curruns;
  while (occ > 0) {
    if (RecoverableThresholdExceeded(sp))
      return false;
    a0 = 0;
    RunLength = 0;
    pa = thisrun;
    SYNC_EOL(eof1d_top, retry_without_eol);
    EXPAND1D(eof1d_a);
    FaxFillRuns(buf, thisrun, pa, static_cast<uint32_t>(lastx));
    buf += sp->rowbytes;
    occ -= sp->rowbytes;
    sp->line++;
    continue;
  eof1d_top:
    CLEANUP_RUNS();
  eof1d_a:
    FaxFillRuns(buf, thisrun, pa, static_cast<uint32_t>(lastx));
    return false;
  }
  return true;
}

}  // namespace

Result<void> DecompressCcittG4(std::span<const uint8_t> compressed,
                               uint32_t width, uint32_t height,
                               FillOrder fill_order,
                               std::vector<uint8_t>& decompressed) {
  if (width == 0 || height == 0) {
    decompressed.clear();
    return Result<void>();
  }
  DecodeState sp;
  if (!AllocateRuns(&sp, width, /*needs_ref_line=*/true)) {
    return AIFOCORE_MAKE_STATUS(
        StatusCode::kInternal,
        "CCITT G4: failed to allocate run buffers (width=" +
            std::to_string(width) + ")");
  }
  // Initialize reference line to all-white (run = rowpixels).
  sp.refruns[0] = sp.rowpixels;
  sp.refruns[1] = 0;
  // libtiff's bit accumulator is filled LSB-first via OR + shift, so for
  // streams stored MSB-first (FillOrder=1, the TIFF default for fax) we must
  // bit-reverse each byte before pushing it into the accumulator. For
  // FillOrder=2 (LSB-first storage) the bytes are already in accumulator
  // order, so no reversal is needed.
  const unsigned char* bitmap =
      (fill_order == FillOrder::kMsb2Lsb) ? kBitRevTable : kNoBitRevTable;
  if (!DecodeG4Rows(&sp, compressed, bitmap, height, decompressed)) {
    return AIFOCORE_MAKE_STATUS(
        StatusCode::kDataLoss,
        "CCITT G4: decode failed (width=" + std::to_string(width) +
            ", height=" + std::to_string(height) + ")");
  }
  return Result<void>();
}

Result<void> DecompressCcittG3(std::span<const uint8_t> compressed,
                               uint32_t width, uint32_t height,
                               FillOrder fill_order,
                               std::vector<uint8_t>& decompressed) {
  if (width == 0 || height == 0) {
    decompressed.clear();
    return Result<void>();
  }
  DecodeState sp;
  if (!AllocateRuns(&sp, width, /*needs_ref_line=*/false)) {
    return AIFOCORE_MAKE_STATUS(
        StatusCode::kInternal,
        "CCITT G3: failed to allocate run buffers (width=" +
            std::to_string(width) + ")");
  }
  const unsigned char* bitmap =
      (fill_order == FillOrder::kMsb2Lsb) ? kBitRevTable : kNoBitRevTable;
  if (!DecodeG3Rows(&sp, compressed, bitmap, height, decompressed)) {
    return AIFOCORE_MAKE_STATUS(
        StatusCode::kDataLoss,
        "CCITT G3: decode failed (width=" + std::to_string(width) +
            ", height=" + std::to_string(height) + ")");
  }
  return Result<void>();
}

Result<void> UnpackOneBitToGray(std::span<const uint8_t> packed, uint32_t width,
                                uint32_t height, uint16_t photometric,
                                std::vector<uint8_t>& unpacked) {
  if (width == 0 || height == 0) {
    unpacked.clear();
    return Result<void>();
  }
  const size_t row_bytes_packed = (static_cast<size_t>(width) + 7U) / 8U;
  const size_t expected_packed_size =
      static_cast<size_t>(height) * row_bytes_packed;
  if (packed.size() < expected_packed_size) {
    return AIFOCORE_MAKE_STATUS(
        StatusCode::kInvalidArgument,
        "UnpackOneBitToGray: packed input too small (need " +
            std::to_string(expected_packed_size) + " bytes, got " +
            std::to_string(packed.size()) + ")");
  }
  unpacked.assign(static_cast<size_t>(height) * width, 0U);

  // Photometric=0 (MinIsWhite): bit 0 -> 255 (white), bit 1 -> 0 (black).
  // Photometric=1 (MinIsBlack) and others: bit 1 -> 255, bit 0 -> 0.
  const bool min_is_white = (photometric == 0);
  const uint8_t one_value = min_is_white ? 0U : 255U;
  const uint8_t zero_value = min_is_white ? 255U : 0U;

  for (uint32_t row = 0; row < height; ++row) {
    const uint8_t* src = packed.data() + row * row_bytes_packed;
    uint8_t* dst = unpacked.data() + static_cast<size_t>(row) * width;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t bit = (src[x >> 3] >> (7U - (x & 7U))) & 1U;
      dst[x] = bit ? one_value : zero_value;
    }
  }
  return Result<void>();
}

}  // namespace simpletiff
