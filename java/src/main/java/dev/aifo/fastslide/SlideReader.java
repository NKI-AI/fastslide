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
import java.util.ArrayList;
import java.util.List;
import java.util.OptionalDouble;

/** Wraps a native {@code FastSlideSlideReader*}. */
public final class SlideReader implements AutoCloseable {

  private MemorySegment handle;

  SlideReader(MemorySegment handle) {
    if (handle.equals(MemorySegment.NULL)) {
      throw new IllegalArgumentException("handle must be non-null");
    }
    this.handle = handle;
  }

  private MemorySegment requireHandle() {
    MemorySegment h = handle;
    if (h == null || h.equals(MemorySegment.NULL)) {
      throw new IllegalStateException("SlideReader is closed");
    }
    return h;
  }

  public int getLevelCount() {
    return FastSlideNative.readerGetLevelCount(requireHandle());
  }

  public Dimensions getBaseDimensions() {
    try (Arena arena = Arena.ofConfined()) {
      int[] dims = FastSlideNative.readerGetBaseDimensions(arena, requireHandle());
      return new Dimensions(dims[0], dims[1]);
    }
  }

  public Dimensions getLevelDimensions(int level) {
    try (Arena arena = Arena.ofConfined()) {
      int[] dims = FastSlideNative.readerGetLevelDimensions(arena, requireHandle(), level);
      return new Dimensions(dims[0], dims[1]);
    }
  }

  public double getLevelDownsample(int level) {
    return FastSlideNative.readerGetLevelDownsample(requireHandle(), level);
  }

  public int getBestLevelForDownsample(double downsample) {
    return FastSlideNative.readerGetBestLevelForDownsample(requireHandle(), downsample);
  }

  public String getFormatName() {
    return FastSlideNative.readerGetFormatName(requireHandle());
  }

  public SlideProperties getProperties() {
    try (Arena arena = Arena.ofConfined()) {
      Object[] raw = FastSlideNative.readerGetProperties(arena, requireHandle());
      return new SlideProperties(
          (double) raw[0],
          (double) raw[1],
          (double) raw[2],
          (String) raw[3],
          (String) raw[4],
          (String) raw[5]);
    }
  }

  public Dimensions getTileSize() {
    try (Arena arena = Arena.ofConfined()) {
      int[] ts = FastSlideNative.readerGetTileSize(arena, requireHandle());
      return new Dimensions(ts[0], ts[1]);
    }
  }

  public ImageFormat getImageFormat() {
    int fmt = FastSlideNative.readerGetImageFormat(requireHandle());
    return ImageFormat.fromNativeValue(fmt);
  }

  public String[] getAssociatedImageNames() {
    try (Arena arena = Arena.ofConfined()) {
      return FastSlideNative.readerGetAssociatedImageNames(arena, requireHandle());
    }
  }

  public Dimensions getAssociatedImageDimensions(String name) {
    try (Arena arena = Arena.ofConfined()) {
      int[] dims = FastSlideNative.readerGetAssociatedImageDimensions(arena, requireHandle(), name);
      return new Dimensions(dims[0], dims[1]);
    }
  }

  /** Returns channel metadata for spectral/fluorescence slides. Empty list for RGB slides. */
  public List<ChannelMetadata> getChannelMetadata() {
    try (Arena arena = Arena.ofConfined()) {
      Object[][] raw = FastSlideNative.readerGetChannelMetadata(arena, requireHandle());
      List<ChannelMetadata> result = new ArrayList<>(raw.length);
      for (Object[] row : raw) {
        result.add(
            new ChannelMetadata(
                (String) row[0],
                (String) row[1],
                (int) row[2],
                (int) row[3],
                (int) row[4],
                (int) row[5],
                (int) row[6]));
      }
      return result;
    }
  }

  // ---------------------------------------------------------------------
  // Multi-image container API
  //
  // A SlideReader is the file/container; a SlideImage is one navigable
  // pyramid inside it. The reader's own level/region methods target the
  // primary image; use getImage() to navigate the others.
  // ---------------------------------------------------------------------

  /** Number of navigable images (pyramids) in this file; always {@code >= 1}. */
  public int getImageCount() {
    return FastSlideNative.readerGetImageCount(requireHandle());
  }

  /** Index of the primary image (the one the reader's own methods target). */
  public int getPrimaryImageIndex() {
    return FastSlideNative.readerGetPrimaryImageIndex(requireHandle());
  }

  /** Human-readable names for every image, in index order. */
  public String[] getImageNames() {
    try (Arena arena = Arena.ofConfined()) {
      return FastSlideNative.readerGetImageNames(arena, requireHandle());
    }
  }

  /**
   * Returns a handle to the {@code index}-th navigable image. The handle borrows this reader and
   * must be {@linkplain SlideImage#close() closed} before, and not used after, the reader is
   * closed.
   *
   * @param index image index in {@code [0, getImageCount())}
   */
  public SlideImage getImage(int index) {
    return new SlideImage(FastSlideNative.readerGetImage(requireHandle(), index));
  }

  /** Z/T stack extent of the primary image. */
  public StackInfo getStackInfo() {
    try (Arena arena = Arena.ofConfined()) {
      Object[] raw = FastSlideNative.readerGetStackInfo(arena, requireHandle());
      return toStackInfo(raw);
    }
  }

  static StackInfo toStackInfo(Object[] raw) {
    int zCount = (int) raw[0];
    int tCount = (int) raw[1];
    boolean hasZSpacing = (boolean) raw[2];
    double zSpacingUm = (double) raw[3];
    boolean hasTInterval = (boolean) raw[4];
    double tIntervalS = (double) raw[5];
    return new StackInfo(
        zCount,
        tCount,
        hasZSpacing ? OptionalDouble.of(zSpacingUm) : OptionalDouble.empty(),
        hasTInterval ? OptionalDouble.of(tIntervalS) : OptionalDouble.empty());
  }

  public Image readRegion(int x, int y, int width, int height, int level) {
    return readRegion(x, y, width, height, level, 0, 0);
  }

  public Image readRegion(int x, int y, int width, int height, int level, int z, int t) {
    if (x < 0 || y < 0 || width < 0 || height < 0) {
      throw new IllegalArgumentException("x/y/width/height must be non-negative");
    }
    if (z < 0 || t < 0) {
      throw new IllegalArgumentException("z/t must be non-negative");
    }
    MemorySegment img =
        FastSlideNative.readerReadRegionCoords(requireHandle(), x, y, width, height, level, z, t);
    return new Image(img);
  }

  @Override
  public void close() {
    MemorySegment h = handle;
    handle = null;
    if (h != null && !h.equals(MemorySegment.NULL)) {
      FastSlideNative.readerFree(h);
    }
  }
}
