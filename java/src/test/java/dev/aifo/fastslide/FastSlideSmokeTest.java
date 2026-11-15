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

    // When a slide path is provided, exercise the Z/T stack API end to end.
    if (args.length > 0) {
      checkStackInfo(args[0]);
      checkIcc(args[0]);
    }
  }

  private static void checkIcc(String path) {
    // Raw profile access and enabling a transform must not throw. Slides
    // without an embedded profile simply report an empty profile and a no-op
    // transform, both of which are valid.
    try (SlideReader reader = FastSlide.open(path)) {
      reader.getIccProfile();
      reader.enableIccTransform(ColorSpace.SRGB, RenderingIntent.PERCEPTUAL);
      try (Image tile = reader.readRegion(0, 0, 1, 1, 0)) {
        if (tile.getSizeBytes() <= 0) {
          throw new AssertionError("Expected non-empty tile after enabling ICC transform");
        }
      }
    }
    // The open-time flag path must also succeed.
    try (SlideReader reader =
        FastSlide.open(path, OpenOptions.withIcc(RenderingIntent.PERCEPTUAL))) {
      try (Image tile = reader.readRegion(0, 0, 1, 1, 0)) {
        if (tile.getSizeBytes() <= 0) {
          throw new AssertionError("Expected non-empty tile when opened with ICC applied");
        }
      }
    }
  }

  private static void checkStackInfo(String path) {
    try (SlideReader reader = FastSlide.open(path)) {
      StackInfo readerStack = reader.getStackInfo();
      if (readerStack.zCount() < 1 || readerStack.tCount() < 1) {
        throw new AssertionError("Expected reader zCount/tCount >= 1, got " + readerStack);
      }
      try (SlideImage image = reader.getImage(reader.getPrimaryImageIndex())) {
        StackInfo imageStack = image.getStackInfo();
        if (imageStack.zCount() < 1 || imageStack.tCount() < 1) {
          throw new AssertionError("Expected image zCount/tCount >= 1, got " + imageStack);
        }
        // Reading the first plane explicitly must succeed.
        try (Image tile = image.readRegion(0, 0, 1, 1, 0, 0, 0)) {
          if (tile.getSizeBytes() <= 0) {
            throw new AssertionError("Expected non-empty tile for plane (z=0, t=0)");
          }
        }
      }
    }
  }
}
