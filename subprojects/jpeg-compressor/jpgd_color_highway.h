/*
 * Fast color conversion using Google Highway SIMD
 * Header file with function declarations
 */

#ifndef JPGD_COLOR_HIGHWAY_H
#define JPGD_COLOR_HIGHWAY_H

#include <stdint.h>

#ifdef __cplusplus

namespace jpgd {

/// @brief Highway-optimized H1V1 RGB conversion (no YCbCr conversion)
/// @param dst Destination buffer for RGBA pixels
/// @param sample_buf Source MCU sample buffer
/// @param row Current row within MCU
/// @param max_mcus_per_row Number of MCUs per row
void ConvertH1V1RGB_HighwayDispatch(uint8_t* dst, const uint8_t* sample_buf,
                                    int row, int max_mcus_per_row);

/// @brief Highway-optimized H1V1 YCbCr to RGB conversion
/// @param dst Destination buffer for RGBA pixels
/// @param sample_buf Source MCU sample buffer
/// @param row Current row within MCU
/// @param max_mcus_per_row Number of MCUs per row
/// @param crr, cbb, crg, cbg Color conversion lookup tables
void ConvertH1V1YCbCr_HighwayDispatch(uint8_t* dst, const uint8_t* sample_buf,
                                      int row, int max_mcus_per_row,
                                      const int* crr, const int* cbb,
                                      const int* crg, const int* cbg);

}  // namespace jpgd

#endif

#endif  // JPGD_COLOR_HIGHWAY_H
