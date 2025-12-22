/*
 * Fast integer IDCT using Google Highway SIMD
 * Implements the IJG integer IDCT algorithm with full vectorization
 */

#include <stdint.h>

#undef HWY_TARGET_INCLUDE
// Highway's foreach_target.h includes HWY_TARGET_INCLUDE multiple times with
// different target defines. The include path differs between:
// - monorepo builds (path usually available as "aifo/jpeg-compressor/...")
// - copybara/public layouts (may be "jpeg-compressor/...")
// - Meson subproject builds (best-effort fallback to the filename)
#if defined(__has_include)
#if __has_include("aifo/jpeg-compressor/jpgd_idct_highway.cpp")
#define HWY_TARGET_INCLUDE "aifo/jpeg-compressor/jpgd_idct_highway.cpp"
#elif __has_include("jpeg-compressor/jpgd_idct_highway.cpp")
#define HWY_TARGET_INCLUDE "jpeg-compressor/jpgd_idct_highway.cpp"
#else
#define HWY_TARGET_INCLUDE "jpgd_idct_highway.cpp"
#endif
#else
#define HWY_TARGET_INCLUDE "jpgd_idct_highway.cpp"
#endif
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace jpgd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

namespace {

// Fixed-point configuration (IJG-style)
constexpr int kConstBits = 13;
constexpr int kPass1Bits = 2;
constexpr int kPass1Shift = kConstBits - kPass1Bits;
constexpr int32_t kPass1Round = 1 << (kPass1Shift - 1);

// IJG integer IDCT constants, CONST_BITS = 13
constexpr int32_t kFix_0_298631336 = 2446;
constexpr int32_t kFix_0_390180644 = 3196;
constexpr int32_t kFix_0_541196100 = 4433;
constexpr int32_t kFix_0_765366865 = 6270;
constexpr int32_t kFix_0_899976223 = 7373;
constexpr int32_t kFix_1_175875602 = 9633;
constexpr int32_t kFix_1_501321110 = 12299;
constexpr int32_t kFix_1_847759065 = 15137;
constexpr int32_t kFix_1_961570560 = 16069;
constexpr int32_t kFix_2_053119869 = 16819;
constexpr int32_t kFix_2_562915447 = 20995;
constexpr int32_t kFix_3_072711026 = 25172;

}  // namespace

// ============================================================================
// Vectorized 1D IDCT Pass 1: Row Processing (Process 2 rows at once)
// ============================================================================

HWY_INLINE void idct_1d_pass1_simd_scalar(int32_t* HWY_RESTRICT temp,
                                          const int16_t* HWY_RESTRICT src) {
  // Scalar fallback for single row
  int32_t c0 = src[0], c1 = src[1], c2 = src[2], c3 = src[3];
  int32_t c4 = src[4], c5 = src[5], c6 = src[6], c7 = src[7];

  // Even part
  int32_t z2 = c2, z3 = c6;
  int32_t z1 = (z2 + z3) * kFix_0_541196100;
  int32_t tmp2 = z1 + z3 * (-kFix_1_847759065);
  int32_t tmp3 = z1 + z2 * kFix_0_765366865;

  int32_t tmp0 = (c0 + c4) << kConstBits;
  int32_t tmp1 = (c0 - c4) << kConstBits;

  int32_t tmp10 = tmp0 + tmp3;
  int32_t tmp13 = tmp0 - tmp3;
  int32_t tmp11 = tmp1 + tmp2;
  int32_t tmp12 = tmp1 - tmp2;

  // Odd part
  int32_t bz1 = c7 + c1, bz2 = c5 + c3, bz3 = c7 + c3, bz4 = c5 + c1;
  int32_t bz5 = (bz3 + bz4) * kFix_1_175875602;

  int32_t az1 = bz1 * (-kFix_0_899976223);
  int32_t az2 = bz2 * (-kFix_2_562915447);
  int32_t az3 = bz3 * (-kFix_1_961570560) + bz5;
  int32_t az4 = bz4 * (-kFix_0_390180644) + bz5;

  int32_t btmp0 = c7 * kFix_0_298631336 + az1 + az3;
  int32_t btmp1 = c5 * kFix_2_053119869 + az2 + az4;
  int32_t btmp2 = c3 * kFix_3_072711026 + az2 + az3;
  int32_t btmp3 = c1 * kFix_1_501321110 + az1 + az4;

  // Final butterfly with rounding
  temp[0] = (tmp10 + btmp3 + kPass1Round) >> kPass1Shift;
  temp[7] = (tmp10 - btmp3 + kPass1Round) >> kPass1Shift;
  temp[1] = (tmp11 + btmp2 + kPass1Round) >> kPass1Shift;
  temp[6] = (tmp11 - btmp2 + kPass1Round) >> kPass1Shift;
  temp[2] = (tmp12 + btmp1 + kPass1Round) >> kPass1Shift;
  temp[5] = (tmp12 - btmp1 + kPass1Round) >> kPass1Shift;
  temp[3] = (tmp13 + btmp0 + kPass1Round) >> kPass1Shift;
  temp[4] = (tmp13 - btmp0 + kPass1Round) >> kPass1Shift;
}

HWY_INLINE void idct_1d_pass1_simd(int32_t* HWY_RESTRICT temp,
                                   const int16_t* HWY_RESTRICT src) {
  // Just use scalar - the broadcast approach was inefficient
  idct_1d_pass1_simd_scalar(temp, src);
}

// ============================================================================
// Vectorized 1D IDCT Pass 2: Column Processing (4 columns at a time)
// ============================================================================

HWY_INLINE void idct_1d_pass2_simd_4cols(uint8_t* HWY_RESTRICT dst,
                                         const int32_t* HWY_RESTRICT temp) {
  const hn::CappedTag<int32_t, 4> d32;

  // Load 4 columns from each of the 8 rows (strided by 8)
  const auto row0 = hn::LoadU(d32, temp + 0 * 8);
  const auto row1 = hn::LoadU(d32, temp + 1 * 8);
  const auto row2 = hn::LoadU(d32, temp + 2 * 8);
  const auto row3 = hn::LoadU(d32, temp + 3 * 8);
  const auto row4 = hn::LoadU(d32, temp + 4 * 8);
  const auto row5 = hn::LoadU(d32, temp + 5 * 8);
  const auto row6 = hn::LoadU(d32, temp + 6 * 8);
  const auto row7 = hn::LoadU(d32, temp + 7 * 8);

  // Load constants as vectors (broadcasted scalars)
  const auto fix_0_541196100 = hn::Set(d32, kFix_0_541196100);
  const auto fix_m1_847759065 = hn::Set(d32, -kFix_1_847759065);
  const auto fix_0_765366865 = hn::Set(d32, kFix_0_765366865);
  const auto fix_1_175875602 = hn::Set(d32, kFix_1_175875602);
  const auto fix_m0_899976223 = hn::Set(d32, -kFix_0_899976223);
  const auto fix_m2_562915447 = hn::Set(d32, -kFix_2_562915447);
  const auto fix_m1_961570560 = hn::Set(d32, -kFix_1_961570560);
  const auto fix_m0_390180644 = hn::Set(d32, -kFix_0_390180644);
  const auto fix_0_298631336 = hn::Set(d32, kFix_0_298631336);
  const auto fix_2_053119869 = hn::Set(d32, kFix_2_053119869);
  const auto fix_3_072711026 = hn::Set(d32, kFix_3_072711026);
  const auto fix_1_501321110 = hn::Set(d32, kFix_1_501321110);

  // --------------------------------------------------------------------------
  // Even part
  // --------------------------------------------------------------------------
  const auto z2 = row2;
  const auto z3 = row6;

  const auto z1 = hn::Mul(hn::Add(z2, z3), fix_0_541196100);
  const auto tmp2 = hn::Add(z1, hn::Mul(z3, fix_m1_847759065));
  const auto tmp3 = hn::Add(z1, hn::Mul(z2, fix_0_765366865));

  const auto tmp0 = hn::ShiftLeft<kConstBits>(hn::Add(row0, row4));
  const auto tmp1 = hn::ShiftLeft<kConstBits>(hn::Sub(row0, row4));

  const auto tmp10 = hn::Add(tmp0, tmp3);
  const auto tmp13 = hn::Sub(tmp0, tmp3);
  const auto tmp11 = hn::Add(tmp1, tmp2);
  const auto tmp12 = hn::Sub(tmp1, tmp2);

  // --------------------------------------------------------------------------
  // Odd part
  // --------------------------------------------------------------------------
  const auto bz1 = hn::Add(row7, row1);
  const auto bz2 = hn::Add(row5, row3);
  const auto bz3 = hn::Add(row7, row3);
  const auto bz4 = hn::Add(row5, row1);
  const auto bz5 = hn::Mul(hn::Add(bz3, bz4), fix_1_175875602);

  const auto az1 = hn::Mul(bz1, fix_m0_899976223);
  const auto az2 = hn::Mul(bz2, fix_m2_562915447);
  const auto az3 = hn::Add(hn::Mul(bz3, fix_m1_961570560), bz5);
  const auto az4 = hn::Add(hn::Mul(bz4, fix_m0_390180644), bz5);

  const auto btmp0 = hn::Add(hn::Add(hn::Mul(row7, fix_0_298631336), az1), az3);
  const auto btmp1 = hn::Add(hn::Add(hn::Mul(row5, fix_2_053119869), az2), az4);
  const auto btmp2 = hn::Add(hn::Add(hn::Mul(row3, fix_3_072711026), az2), az3);
  const auto btmp3 = hn::Add(hn::Add(hn::Mul(row1, fix_1_501321110), az1), az4);

  // --------------------------------------------------------------------------
  // Final butterfly with descaling and zero-shift
  // --------------------------------------------------------------------------
  constexpr int32_t kDescaleShift = kConstBits + kPass1Bits + 3;
  constexpr int32_t kRoundVal =
      (128 << kDescaleShift) + (1 << (kDescaleShift - 1));
  const auto round = hn::Set(d32, kRoundVal);

  const auto out0 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Add(tmp10, btmp3), round));
  const auto out7 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Sub(tmp10, btmp3), round));
  const auto out1 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Add(tmp11, btmp2), round));
  const auto out6 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Sub(tmp11, btmp2), round));
  const auto out2 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Add(tmp12, btmp1), round));
  const auto out5 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Sub(tmp12, btmp1), round));
  const auto out3 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Add(tmp13, btmp0), round));
  const auto out4 =
      hn::ShiftRight<kDescaleShift>(hn::Add(hn::Sub(tmp13, btmp0), round));

  // Clamp to [0, 255]
  const auto zero = hn::Zero(d32);
  const auto max_val = hn::Set(d32, 255);

  const auto clamped0 = hn::Min(hn::Max(out0, zero), max_val);
  const auto clamped1 = hn::Min(hn::Max(out1, zero), max_val);
  const auto clamped2 = hn::Min(hn::Max(out2, zero), max_val);
  const auto clamped3 = hn::Min(hn::Max(out3, zero), max_val);
  const auto clamped4 = hn::Min(hn::Max(out4, zero), max_val);
  const auto clamped5 = hn::Min(hn::Max(out5, zero), max_val);
  const auto clamped6 = hn::Min(hn::Max(out6, zero), max_val);
  const auto clamped7 = hn::Min(hn::Max(out7, zero), max_val);

  // Convert int32 -> int16 -> uint8 and store (4 values per row)
  const hn::Rebind<int16_t, decltype(d32)> d16;
  const hn::Rebind<uint8_t, decltype(d32)> d8;

  const auto i16_0 = hn::DemoteTo(d16, clamped0);
  const auto i16_1 = hn::DemoteTo(d16, clamped1);
  const auto i16_2 = hn::DemoteTo(d16, clamped2);
  const auto i16_3 = hn::DemoteTo(d16, clamped3);
  const auto i16_4 = hn::DemoteTo(d16, clamped4);
  const auto i16_5 = hn::DemoteTo(d16, clamped5);
  const auto i16_6 = hn::DemoteTo(d16, clamped6);
  const auto i16_7 = hn::DemoteTo(d16, clamped7);

  const auto u8_0 = hn::DemoteTo(d8, i16_0);
  const auto u8_1 = hn::DemoteTo(d8, i16_1);
  const auto u8_2 = hn::DemoteTo(d8, i16_2);
  const auto u8_3 = hn::DemoteTo(d8, i16_3);
  const auto u8_4 = hn::DemoteTo(d8, i16_4);
  const auto u8_5 = hn::DemoteTo(d8, i16_5);
  const auto u8_6 = hn::DemoteTo(d8, i16_6);
  const auto u8_7 = hn::DemoteTo(d8, i16_7);

  hn::StoreU(u8_0, d8, dst + 0 * 8);
  hn::StoreU(u8_1, d8, dst + 1 * 8);
  hn::StoreU(u8_2, d8, dst + 2 * 8);
  hn::StoreU(u8_3, d8, dst + 3 * 8);
  hn::StoreU(u8_4, d8, dst + 4 * 8);
  hn::StoreU(u8_5, d8, dst + 5 * 8);
  hn::StoreU(u8_6, d8, dst + 6 * 8);
  hn::StoreU(u8_7, d8, dst + 7 * 8);
}

HWY_INLINE void idct_1d_pass2_simd(uint8_t* HWY_RESTRICT dst,
                                   const int32_t* HWY_RESTRICT temp) {
  // Process first 4 columns
  idct_1d_pass2_simd_4cols(dst, temp);
  // Process last 4 columns
  idct_1d_pass2_simd_4cols(dst + 4, temp + 4);
}

// ============================================================================
// Main 8x8 IDCT Function
// ============================================================================

void IdctHighway(const int16_t* HWY_RESTRICT input,
                 uint8_t* HWY_RESTRICT output) {
  // Temporary storage for intermediate values after row processing
  int32_t temp[64];

  // Pass 1: Process rows with vectorized IDCT
  for (int row = 0; row < 8; row++) {
    idct_1d_pass1_simd(temp + row * 8, input + row * 8);
  }

  // Pass 2: Process all columns in parallel with vectorized IDCT
  idct_1d_pass2_simd(output, temp);
}

}  // namespace HWY_NAMESPACE
}  // namespace jpgd

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace jpgd {
HWY_EXPORT(IdctHighway);

/// @brief Highway-optimized 8x8 IDCT dispatch function
/// @param input Input DCT coefficients (8x8 block as row-major int16)
/// @param output Output pixel values (8x8 block as row-major uint8)
void IdctHighwayDispatch(const int16_t* input, uint8_t* output) {
  return HWY_DYNAMIC_DISPATCH(IdctHighway)(input, output);
}

}  // namespace jpgd

// C interface
extern "C" {
void idctHighwayShortU8(const int16_t* input, uint8_t* output) {
  jpgd::IdctHighwayDispatch(input, output);
}
}

#endif  // HWY_ONCE
