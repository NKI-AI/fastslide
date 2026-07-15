// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/**
 * Options controlling how a slide is opened.
 *
 * <p>When {@link #applyIcc()} is {@code true} and the slide carries an embedded ICC profile, {@link
 * SlideReader#readRegion} returns pixels already converted to {@link #targetColorSpace()} using
 * {@link #renderingIntent()}. Defaults ({@link #defaults()}) leave color unmanaged.
 *
 * @param applyIcc apply the embedded ICC profile during {@code readRegion}
 * @param targetColorSpace destination color space for the transform
 * @param renderingIntent ICC rendering intent for the transform
 */
public record OpenOptions(
    boolean applyIcc, ColorSpace targetColorSpace, RenderingIntent renderingIntent) {

  public OpenOptions {
    if (targetColorSpace == null) {
      throw new IllegalArgumentException("targetColorSpace must not be null");
    }
    if (renderingIntent == null) {
      throw new IllegalArgumentException("renderingIntent must not be null");
    }
  }

  /** Defaults: no ICC color management. */
  public static OpenOptions defaults() {
    return new OpenOptions(false, ColorSpace.SRGB, RenderingIntent.PERCEPTUAL);
  }

  /** Apply the embedded ICC profile, converting to sRGB with the given intent. */
  public static OpenOptions withIcc(RenderingIntent intent) {
    return new OpenOptions(true, ColorSpace.SRGB, intent);
  }
}
