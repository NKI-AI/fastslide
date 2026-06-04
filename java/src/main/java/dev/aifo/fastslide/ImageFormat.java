// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Image format as reported by the FastSlide C API. */
public enum ImageFormat {
  // Matches fastslide/c/image.h (FastSlideImageFormat).
  GRAY(1),
  RGB(3),
  RGBA(4),
  SPECTRAL(0);

  private final int nativeValue;

  ImageFormat(int nativeValue) {
    this.nativeValue = nativeValue;
  }

  public int nativeValue() {
    return nativeValue;
  }

  public static ImageFormat fromNativeValue(long nativeValue) {
    for (ImageFormat v : values()) {
      if (v.nativeValue == (int) nativeValue) {
        return v;
      }
    }
    throw new IllegalArgumentException("Unknown ImageFormat native value: " + nativeValue);
  }
}
