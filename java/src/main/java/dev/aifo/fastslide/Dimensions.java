// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Image dimensions. */
public final class Dimensions {
  private final int width;
  private final int height;

  public Dimensions(int width, int height) {
    if (width < 0 || height < 0) {
      throw new IllegalArgumentException("width/height must be non-negative");
    }
    this.width = width;
    this.height = height;
  }

  public int width() {
    return width;
  }

  public int height() {
    return height;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) {
      return true;
    }
    if (!(o instanceof Dimensions)) {
      return false;
    }
    Dimensions that = (Dimensions) o;
    return width == that.width && height == that.height;
  }

  @Override
  public int hashCode() {
    int result = width;
    result = 31 * result + height;
    return result;
  }

  @Override
  public String toString() {
    return "Dimensions{width=" + width + ", height=" + height + "}";
  }
}
