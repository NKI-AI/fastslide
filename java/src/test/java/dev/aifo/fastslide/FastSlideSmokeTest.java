// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

import java.util.List;

/** Smoke test entrypoint for Bazel's {@code java_test} without JUnit. */
public final class FastSlideSmokeTest {
  public static void main(String[] args) {
    String cApiVersion = FastSlide.getCApiVersion();
    if (cApiVersion == null || cApiVersion.isEmpty()) {
      throw new AssertionError("Expected non-empty C API version");
    }

    String version = FastSlide.getVersion();
    if (version == null || version.isEmpty()) {
      throw new AssertionError("Expected non-empty FastSlide version");
    }

    List<String> extensions = FastSlide.getSupportedExtensions();
    if (extensions == null) {
      throw new AssertionError("Expected non-null supported extensions list");
    }

    // Should not throw.
    FastSlide.isSupported("this-file-does-not-exist.svs");
  }
}
