// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Memory layout of pixel data as reported by the FastSlide C API. */
public enum PlanarConfig {
  // Matches fastslide/c/image.h (FastSlidePlanarConfig).
  CONTIG(1),
  SEPARATE(2);

  private final int nativeValue;

  PlanarConfig(int nativeValue) {
    this.nativeValue = nativeValue;
  }

  public int nativeValue() {
    return nativeValue;
  }

  public static PlanarConfig fromNativeValue(long nativeValue) {
    for (PlanarConfig v : values()) {
      if (v.nativeValue == (int) nativeValue) {
        return v;
      }
    }
    throw new IllegalArgumentException("Unknown PlanarConfig native value: " + nativeValue);
  }
}
