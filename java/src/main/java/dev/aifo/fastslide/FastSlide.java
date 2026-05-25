// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

import dev.aifo.fastslide.internal.FastSlideNative;
import java.lang.foreign.Arena;
import java.util.Arrays;
import java.util.List;

/** High-level Java bindings for the FastSlide C API via FFM. */
public final class FastSlide {
  private static volatile boolean initialized = false;

  private FastSlide() {}

  private static void ensureInitialized() {
    if (initialized) return;
    synchronized (FastSlide.class) {
      if (initialized) return;
      FastSlideNative.initialize();
      initialized = true;
    }
  }

  public static String getVersion() {
    ensureInitialized();
    return FastSlideNative.getVersion();
  }

  public static String getCApiVersion() {
    ensureInitialized();
    return FastSlideNative.getCApiVersion();
  }

  public static List<String> getSupportedExtensions() {
    ensureInitialized();
    try (Arena arena = Arena.ofConfined()) {
      return Arrays.asList(FastSlideNative.getSupportedExtensions(arena));
    }
  }

  public static boolean isSupported(String path) {
    ensureInitialized();
    try (Arena arena = Arena.ofConfined()) {
      return FastSlideNative.isSupported(arena, path);
    }
  }

  public static SlideReader open(String path) {
    ensureInitialized();
    var reader = FastSlideNative.createReader(Arena.global(), path);
    return new SlideReader(reader);
  }
}
