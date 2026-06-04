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

/**
 * One navigable pyramid (a "series" / "scene") inside a slide file.
 *
 * <p>A {@code SlideImage} is a stateless view borrowed from its owning {@link SlideReader}; it must
 * not be used after the reader is closed. Obtain one with {@link SlideReader#getImage(int)}. Most
 * formats expose a single image (use {@link SlideReader} directly); formats such as Olympus VSI
 * expose several (a low-resolution navigator plus one main scan per imaged region).
 */
public final class SlideImage implements AutoCloseable {

  private MemorySegment handle;

  SlideImage(MemorySegment handle) {
    if (handle.equals(MemorySegment.NULL)) {
      throw new IllegalArgumentException("handle must be non-null");
    }
    this.handle = handle;
  }

  private MemorySegment requireHandle() {
    MemorySegment h = handle;
    if (h == null || h.equals(MemorySegment.NULL)) {
      throw new IllegalStateException("SlideImage is closed");
    }
    return h;
  }

  public int getLevelCount() {
    return FastSlideNative.slideImageGetLevelCount(requireHandle());
  }

  public Dimensions getBaseDimensions() {
    try (Arena arena = Arena.ofConfined()) {
      int[] dims = FastSlideNative.slideImageGetBaseDimensions(arena, requireHandle());
      return new Dimensions(dims[0], dims[1]);
    }
  }

  public Dimensions getLevelDimensions(int level) {
    try (Arena arena = Arena.ofConfined()) {
      int[] dims = FastSlideNative.slideImageGetLevelDimensions(arena, requireHandle(), level);
      return new Dimensions(dims[0], dims[1]);
    }
  }

  public double getLevelDownsample(int level) {
    return FastSlideNative.slideImageGetLevelDownsample(requireHandle(), level);
  }

  public Dimensions getTileSize() {
    try (Arena arena = Arena.ofConfined()) {
      int[] ts = FastSlideNative.slideImageGetTileSize(arena, requireHandle());
      return new Dimensions(ts[0], ts[1]);
    }
  }

  public ImageFormat getImageFormat() {
    int fmt = FastSlideNative.slideImageGetImageFormat(requireHandle());
    return ImageFormat.fromNativeValue(fmt);
  }

  public DataType getDataType() {
    int dt = FastSlideNative.slideImageGetDataType(requireHandle());
    return DataType.fromNativeValue(dt);
  }

  public SlideProperties getProperties() {
    try (Arena arena = Arena.ofConfined()) {
      Object[] raw = FastSlideNative.slideImageGetProperties(arena, requireHandle());
      return new SlideProperties(
          (double) raw[0],
          (double) raw[1],
          (double) raw[2],
          (String) raw[3],
          (String) raw[4],
          (String) raw[5]);
    }
  }

  /** Returns channel metadata for spectral/fluorescence images. Empty list for RGB images. */
  public List<ChannelMetadata> getChannelMetadata() {
    try (Arena arena = Arena.ofConfined()) {
      Object[][] raw = FastSlideNative.slideImageGetChannelMetadata(arena, requireHandle());
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

  /** Z/T stack extent of this image. */
  public StackInfo getStackInfo() {
    try (Arena arena = Arena.ofConfined()) {
      Object[] raw = FastSlideNative.slideImageGetStackInfo(arena, requireHandle());
      return SlideReader.toStackInfo(raw);
    }
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
        FastSlideNative.slideImageReadRegionCoords(
            requireHandle(), x, y, width, height, level, z, t);
    return new Image(img);
  }

  @Override
  public void close() {
    MemorySegment h = handle;
    handle = null;
    if (h != null && !h.equals(MemorySegment.NULL)) {
      FastSlideNative.slideImageFree(h);
    }
  }
}
