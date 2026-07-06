// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** ICC rendering intent for color management. Matches {@code FastSlideRenderingIntent}. */
public enum RenderingIntent {
  // Matches fastslide/c/slide_reader.h (FastSlideRenderingIntent).
  PERCEPTUAL(0),
  RELATIVE_COLORIMETRIC(1),
  SATURATION(2),
  ABSOLUTE_COLORIMETRIC(3);

  private final int nativeValue;

  RenderingIntent(int nativeValue) {
    this.nativeValue = nativeValue;
  }

  public int nativeValue() {
    return nativeValue;
  }

  public static RenderingIntent fromNativeValue(long nativeValue) {
    for (RenderingIntent v : values()) {
      if (v.nativeValue == (int) nativeValue) {
        return v;
      }
    }
    throw new IllegalArgumentException("Unknown RenderingIntent native value: " + nativeValue);
  }
}
