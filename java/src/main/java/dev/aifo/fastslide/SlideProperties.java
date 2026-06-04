// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Slide-level properties (MPP, magnification, scanner info). */
public final class SlideProperties {
  private final double mppX;
  private final double mppY;
  private final double objectiveMagnification;
  private final String objectiveName;
  private final String scannerModel;
  private final String scanDate;

  public SlideProperties(
      double mppX,
      double mppY,
      double objectiveMagnification,
      String objectiveName,
      String scannerModel,
      String scanDate) {
    this.mppX = mppX;
    this.mppY = mppY;
    this.objectiveMagnification = objectiveMagnification;
    this.objectiveName = objectiveName != null ? objectiveName : "";
    this.scannerModel = scannerModel != null ? scannerModel : "";
    this.scanDate = scanDate != null ? scanDate : "";
  }

  public double mppX() {
    return mppX;
  }

  public double mppY() {
    return mppY;
  }

  public double objectiveMagnification() {
    return objectiveMagnification;
  }

  public String objectiveName() {
    return objectiveName;
  }

  public String scannerModel() {
    return scannerModel;
  }

  /** Returns the scan date string, or empty if not available. */
  public String scanDate() {
    return scanDate;
  }

  @Override
  public String toString() {
    return "SlideProperties{mppX="
        + mppX
        + ", mppY="
        + mppY
        + ", objectiveMagnification="
        + objectiveMagnification
        + ", objectiveName='"
        + objectiveName
        + '\''
        + ", scannerModel='"
        + scannerModel
        + '\''
        + ", scanDate='"
        + scanDate
        + '\''
        + '}';
  }
}
