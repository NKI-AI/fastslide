// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Pixel data type as reported by the FastSlide C API. */
public enum DataType {
  // Matches fastslide/c/image.h (FastSlideDataType).
  UINT8(0),
  UINT16(1),
  INT16(2),
  UINT32(3),
  INT32(4),
  FLOAT32(5),
  FLOAT64(6);

  private final int nativeValue;

  DataType(int nativeValue) {
    this.nativeValue = nativeValue;
  }

  public int nativeValue() {
    return nativeValue;
  }

  public static DataType fromNativeValue(long nativeValue) {
    for (DataType v : values()) {
      if (v.nativeValue == (int) nativeValue) {
        return v;
      }
    }
    throw new IllegalArgumentException("Unknown DataType native value: " + nativeValue);
  }
}
