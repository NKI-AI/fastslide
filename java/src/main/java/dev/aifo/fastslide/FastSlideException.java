// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Thrown when the FastSlide native library reports an error. */
public final class FastSlideException extends RuntimeException {
  public FastSlideException(String message) {
    super(message);
  }

  public FastSlideException(String message, Throwable cause) {
    super(message, cause);
  }
}
