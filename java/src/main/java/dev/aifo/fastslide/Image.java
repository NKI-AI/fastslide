// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

import dev.aifo.fastslide.internal.FastSlideNative;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

/** Wraps a native {@code FastSlideImage*}. */
public final class Image implements AutoCloseable {

  private MemorySegment handle;

  Image(MemorySegment handle) {
    if (handle.equals(MemorySegment.NULL)) {
      throw new IllegalArgumentException("handle must be non-null");
    }
    this.handle = handle;
  }

  private MemorySegment requireHandle() {
    MemorySegment h = handle;
    if (h == null || h.equals(MemorySegment.NULL)) {
      throw new IllegalStateException("Image is closed");
    }
    return h;
  }

  public ImageInfo getInfo() {
    try (Arena arena = Arena.ofConfined()) {
      long[] packed = FastSlideNative.imageGetInfo(arena, requireHandle());
      return new ImageInfo(
          ImageFormat.fromNativeValue(packed[0]),
          DataType.fromNativeValue(packed[1]),
          PlanarConfig.fromNativeValue(packed[2]),
          (int) packed[3],
          (int) packed[4],
          (int) packed[5],
          packed[6],
          packed[7]);
    }
  }

  public long getSizeBytes() {
    return FastSlideNative.imageGetSizeBytes(requireHandle());
  }

  /** Copies the native pixel buffer into a Java byte array. */
  public byte[] copyData() {
    try (Arena arena = Arena.ofConfined()) {
      return FastSlideNative.imageCopyData(arena, requireHandle());
    }
  }

  @Override
  public void close() {
    MemorySegment h = handle;
    handle = null;
    if (h != null && !h.equals(MemorySegment.NULL)) {
      FastSlideNative.imageFree(h);
    }
  }
}
