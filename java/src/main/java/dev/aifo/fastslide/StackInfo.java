// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide;

import java.util.OptionalDouble;

/**
 * Z (focal) / T (time) stack extent for an image.
 *
 * <p>{@code zCount} and {@code tCount} are always {@code >= 1}. The spacings are optional and only
 * present when the source file records them.
 *
 * @param zCount number of focal planes (>= 1)
 * @param tCount number of time points (>= 1)
 * @param zSpacingMicrons focal-plane spacing in microns, if known
 * @param tIntervalSeconds time-point interval in seconds, if known
 */
public record StackInfo(
    int zCount, int tCount, OptionalDouble zSpacingMicrons, OptionalDouble tIntervalSeconds) {}
