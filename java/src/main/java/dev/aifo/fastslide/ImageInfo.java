// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Information about an image returned by FastSlide. */
public final class ImageInfo {
  private final ImageFormat format;
  private final DataType dataType;
  private final PlanarConfig planarConfig;
  private final int width;
  private final int height;
  private final int channels;
  private final long bytesPerSample;
  private final long dataSize;

  public ImageInfo(
      ImageFormat format,
      DataType dataType,
      PlanarConfig planarConfig,
      int width,
      int height,
      int channels,
      long bytesPerSample,
      long dataSize) {
    if (format == null || dataType == null || planarConfig == null) {
      throw new IllegalArgumentException("format/dataType/planarConfig must be non-null");
    }
    if (width < 0 || height < 0 || channels < 0) {
      throw new IllegalArgumentException("width/height/channels must be non-negative");
    }
    if (bytesPerSample < 0 || dataSize < 0) {
      throw new IllegalArgumentException("bytesPerSample/dataSize must be non-negative");
    }
    this.format = format;
    this.dataType = dataType;
    this.planarConfig = planarConfig;
    this.width = width;
    this.height = height;
    this.channels = channels;
    this.bytesPerSample = bytesPerSample;
    this.dataSize = dataSize;
  }

  public ImageFormat format() {
    return format;
  }

  public DataType dataType() {
    return dataType;
  }

  public PlanarConfig planarConfig() {
    return planarConfig;
  }

  public int width() {
    return width;
  }

  public int height() {
    return height;
  }

  public int channels() {
    return channels;
  }

  public long bytesPerSample() {
    return bytesPerSample;
  }

  public long dataSize() {
    return dataSize;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) {
      return true;
    }
    if (!(o instanceof ImageInfo)) {
      return false;
    }
    ImageInfo that = (ImageInfo) o;
    return width == that.width
        && height == that.height
        && channels == that.channels
        && bytesPerSample == that.bytesPerSample
        && dataSize == that.dataSize
        && format == that.format
        && dataType == that.dataType
        && planarConfig == that.planarConfig;
  }

  @Override
  public int hashCode() {
    int result = format.hashCode();
    result = 31 * result + dataType.hashCode();
    result = 31 * result + planarConfig.hashCode();
    result = 31 * result + width;
    result = 31 * result + height;
    result = 31 * result + channels;
    result = 31 * result + (int) (bytesPerSample ^ (bytesPerSample >>> 32));
    result = 31 * result + (int) (dataSize ^ (dataSize >>> 32));
    return result;
  }

  @Override
  public String toString() {
    return "ImageInfo{"
        + "format="
        + format
        + ", dataType="
        + dataType
        + ", planarConfig="
        + planarConfig
        + ", width="
        + width
        + ", height="
        + height
        + ", channels="
        + channels
        + ", bytesPerSample="
        + bytesPerSample
        + ", dataSize="
        + dataSize
        + "}";
  }
}
