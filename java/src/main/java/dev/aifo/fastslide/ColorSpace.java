// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Target color space for ICC color management. Matches {@code FastSlideColorSpace}. */
public enum ColorSpace {
  // Matches fastslide/c/slide_reader.h (FastSlideColorSpace).
  RGB(0),
  LINEAR(1),
  SRGB(2),
  AUTOMATIC(3);

  private final int nativeValue;

  ColorSpace(int nativeValue) {
    this.nativeValue = nativeValue;
  }

  public int nativeValue() {
    return nativeValue;
  }

  public static ColorSpace fromNativeValue(long nativeValue) {
    for (ColorSpace v : values()) {
      if (v.nativeValue == (int) nativeValue) {
        return v;
      }
    }
    throw new IllegalArgumentException("Unknown ColorSpace native value: " + nativeValue);
  }
}
