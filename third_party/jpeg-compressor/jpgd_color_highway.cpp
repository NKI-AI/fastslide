/*
 * Fast color conversion using Google Highway SIMD
 * Implements YCbCr to RGB and RGB passthrough conversions
 */

#include <stdint.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "aifo/jpeg-compressor/jpgd_color_highway.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace jpgd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// ============================================================================
// Vectorized Color Conversion: H1V1 RGB (No YCbCr conversion)
// ============================================================================

void ConvertH1V1RGB_Highway(uint8_t* HWY_RESTRICT dst,
                            const uint8_t* HWY_RESTRICT sample_buf, int row,
                            int max_mcus_per_row) {
  const hn::ScalableTag<uint8_t> d8;
  const size_t N = hn::Lanes(d8);

  uint8_t* d = dst;
  const uint8_t* s = sample_buf + row * 8;

  for (int mcu = 0; mcu < max_mcus_per_row; mcu++) {
    // Process 8 pixels per MCU
    // For H1V1: 3 blocks of 64 bytes each (Y, Cb, Cr)
    // We need to read 8 consecutive values from each block

    // Load Y, Cb, Cr values (8 pixels)
    const auto y_vals = hn::LoadU(d8, s);         // Y (treated as R)
    const auto cb_vals = hn::LoadU(d8, s + 64);   // Cb (treated as G)
    const auto cr_vals = hn::LoadU(d8, s + 128);  // Cr (treated as B)

    // Store as RGBA - need to interleave
    // Since we need RGBA output, we'll do this in a simple loop
    // (Full SIMD interleave would require more complex shuffling)
    for (size_t i = 0; i < 8; i++) {
      d[i * 4 + 0] = hn::ExtractLane(y_vals, i);   // R
      d[i * 4 + 1] = hn::ExtractLane(cb_vals, i);  // G
      d[i * 4 + 2] = hn::ExtractLane(cr_vals, i);  // B
      d[i * 4 + 3] = 255;                          // A
    }

    d += 32;      // 8 pixels * 4 bytes
    s += 64 * 3;  // 3 blocks of 64 bytes
  }
}

// ============================================================================
// Vectorized Color Conversion: H1V1 YCbCr to RGB
// ============================================================================

void ConvertH1V1YCbCr_Highway(uint8_t* HWY_RESTRICT dst,
                              const uint8_t* HWY_RESTRICT sample_buf, int row,
                              int max_mcus_per_row, const int* crr,
                              const int* cbb, const int* crg, const int* cbg) {
  const hn::ScalableTag<int32_t> d32;
  const hn::Rebind<uint8_t, decltype(d32)> d8;
  const size_t N32 = hn::Lanes(d32);

  uint8_t* d = dst;
  const uint8_t* s = sample_buf + row * 8;

  for (int mcu = 0; mcu < max_mcus_per_row; mcu++) {
    // Process 8 pixels - vectorize in chunks of N32
    for (size_t base = 0; base < 8; base += N32) {
      const size_t count = (base + N32 <= 8) ? N32 : (8 - base);

      // Load Y, Cb, Cr values
      int32_t y_vals[4], cb_vals[4], cr_vals[4];
      for (size_t i = 0; i < count; i++) {
        y_vals[i] = s[base + i];
        cb_vals[i] = s[64 + base + i];
        cr_vals[i] = s[128 + base + i];
      }

      auto y_vec = hn::LoadU(d32, y_vals);
      auto cb_vec = hn::LoadU(d32, cb_vals);
      auto cr_vec = hn::LoadU(d32, cr_vals);

      // Load color conversion values from lookup tables
      int32_t crr_vals[4], cbb_vals[4], crg_vals[4], cbg_vals[4];
      for (size_t i = 0; i < count; i++) {
        crr_vals[i] = crr[cr_vals[i]];
        cbb_vals[i] = cbb[cb_vals[i]];
        crg_vals[i] = crg[cr_vals[i]];
        cbg_vals[i] = cbg[cb_vals[i]];
      }

      auto crr_vec = hn::LoadU(d32, crr_vals);
      auto cbb_vec = hn::LoadU(d32, cbb_vals);
      auto crg_vec = hn::LoadU(d32, crg_vals);
      auto cbg_vec = hn::LoadU(d32, cbg_vals);

      // Compute RGB
      auto r_vec = hn::Add(y_vec, crr_vec);
      auto b_vec = hn::Add(y_vec, cbb_vec);

      // G requires shifting: g = y + ((crg + cbg) >> 16)
      auto g_sum = hn::Add(crg_vec, cbg_vec);
      auto g_shifted = hn::ShiftRight<16>(g_sum);
      auto g_vec = hn::Add(y_vec, g_shifted);

      // Clamp to [0, 255]
      const auto zero = hn::Zero(d32);
      const auto max_val = hn::Set(d32, 255);

      r_vec = hn::Min(hn::Max(r_vec, zero), max_val);
      g_vec = hn::Min(hn::Max(g_vec, zero), max_val);
      b_vec = hn::Min(hn::Max(b_vec, zero), max_val);

      // Store results
      for (size_t i = 0; i < count; i++) {
        d[(base + i) * 4 + 0] = static_cast<uint8_t>(hn::ExtractLane(r_vec, i));
        d[(base + i) * 4 + 1] = static_cast<uint8_t>(hn::ExtractLane(g_vec, i));
        d[(base + i) * 4 + 2] = static_cast<uint8_t>(hn::ExtractLane(b_vec, i));
        d[(base + i) * 4 + 3] = 255;
      }
    }

    d += 32;      // 8 pixels * 4 bytes
    s += 64 * 3;  // 3 blocks
  }
}

}  // namespace HWY_NAMESPACE
}  // namespace jpgd

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace jpgd {
HWY_EXPORT(ConvertH1V1RGB_Highway);
HWY_EXPORT(ConvertH1V1YCbCr_Highway);

void ConvertH1V1RGB_HighwayDispatch(uint8_t* dst, const uint8_t* sample_buf,
                                    int row, int max_mcus_per_row) {
  return HWY_DYNAMIC_DISPATCH(ConvertH1V1RGB_Highway)(dst, sample_buf, row,
                                                      max_mcus_per_row);
}

void ConvertH1V1YCbCr_HighwayDispatch(uint8_t* dst, const uint8_t* sample_buf,
                                      int row, int max_mcus_per_row,
                                      const int* crr, const int* cbb,
                                      const int* crg, const int* cbg) {
  return HWY_DYNAMIC_DISPATCH(ConvertH1V1YCbCr_Highway)(
      dst, sample_buf, row, max_mcus_per_row, crr, cbb, crg, cbg);
}

}  // namespace jpgd

#endif  // HWY_ONCE
