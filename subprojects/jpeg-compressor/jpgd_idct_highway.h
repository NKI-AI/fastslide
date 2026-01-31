/*
 * Fast integer IDCT (Google Highway - Optimized)
 * Header file with function declaration
 */

#ifndef JPGD_IDCT_HIGHWAY_H
#define JPGD_IDCT_HIGHWAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Highway-optimized IDCT function
void idctHighwayShortU8(const int16_t* input, uint8_t* output);

#ifdef __cplusplus
}
#endif

#endif  // JPGD_IDCT_HIGHWAY_H
