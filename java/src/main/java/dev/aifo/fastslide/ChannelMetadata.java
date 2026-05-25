// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

/** Metadata for a single channel in a spectral/fluorescence slide. */
public record ChannelMetadata(
    String name,
    String biomarker,
    int colorR,
    int colorG,
    int colorB,
    int exposureTime,
    int signalUnits) {}
