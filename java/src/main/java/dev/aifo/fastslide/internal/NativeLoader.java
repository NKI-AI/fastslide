// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide.internal;

import dev.aifo.fastslide.FastSlideException;
import java.io.IOException;
import java.io.InputStream;
import java.lang.foreign.Arena;
import java.lang.foreign.SymbolLookup;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Locale;

/**
 * Locates the FastSlide native shared library and exposes it as a {@link SymbolLookup} for FFM
 * downcalls.
 *
 * <p>Resolution order:
 *
 * <ol>
 *   <li>{@code -Dfastslide.native.path=<file>} (explicit path, e.g. from Bazel)
 *   <li>Classpath probe at {@code META-INF/native/<os>-<arch>/<libname>} (classifier JAR)
 *   <li>System library lookup for {@code "fastslide"}
 * </ol>
 */
public final class NativeLoader {
  private static final String NATIVE_PATH_PROPERTY = "fastslide.native.path";
  private static final String LEGACY_JNI_PATH_PROPERTY = "fastslide.jni.path";
  private static final String NATIVE_RESOURCE_PREFIX = "META-INF/native/";

  private static volatile SymbolLookup cachedLookup;

  private NativeLoader() {}

  public static SymbolLookup lookup() {
    SymbolLookup result = cachedLookup;
    if (result != null) return result;

    synchronized (NativeLoader.class) {
      result = cachedLookup;
      if (result != null) return result;

      result = loadLookup();
      cachedLookup = result;
      return result;
    }
  }

  private static SymbolLookup loadLookup() {
    String explicitPath = System.getProperty(NATIVE_PATH_PROPERTY, "").trim();
    if (explicitPath.isEmpty()) {
      explicitPath = System.getProperty(LEGACY_JNI_PATH_PROPERTY, "").trim();
    }

    if (!explicitPath.isEmpty()) {
      return SymbolLookup.libraryLookup(Path.of(explicitPath), Arena.global());
    }

    SymbolLookup classpathLookup = tryLoadFromClasspath();
    if (classpathLookup != null) {
      return classpathLookup;
    }

    try {
      System.loadLibrary("fastslide");
      return SymbolLookup.loaderLookup();
    } catch (UnsatisfiedLinkError e) {
      throw new FastSlideException(
          "Failed to load FastSlide native library. "
              + "Ensure the classifier JAR for "
              + osName()
              + "-"
              + archName()
              + " is on the classpath, or set -D"
              + NATIVE_PATH_PROPERTY
              + "=<path>.",
          e);
    }
  }

  private static SymbolLookup tryLoadFromClasspath() {
    String resourcePath =
        NATIVE_RESOURCE_PREFIX + osName() + "-" + archName() + "/" + libFilename();
    InputStream stream = NativeLoader.class.getClassLoader().getResourceAsStream(resourcePath);
    if (stream == null) return null;

    try {
      Path tempFile = Files.createTempFile("fastslide_", libSuffix());
      tempFile.toFile().deleteOnExit();
      Files.copy(stream, tempFile, StandardCopyOption.REPLACE_EXISTING);
      stream.close();
      return SymbolLookup.libraryLookup(tempFile, Arena.global());
    } catch (IOException e) {
      throw new FastSlideException(
          "Found native library on classpath at '" + resourcePath + "' but failed to extract it.",
          e);
    }
  }

  static String osName() {
    String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
    if (os.contains("linux")) return "linux";
    if (os.contains("mac") || os.contains("darwin")) return "darwin";
    if (os.contains("win")) return "windows";
    return os.replaceAll("\\s+", "_");
  }

  static String archName() {
    String arch = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);
    return switch (arch) {
      case "amd64", "x86_64" -> "x86_64";
      case "aarch64", "arm64" -> "aarch64";
      default -> arch;
    };
  }

  private static String libFilename() {
    return switch (osName()) {
      case "windows" -> "fastslide.dll";
      case "darwin" -> "libfastslide.dylib";
      default -> "libfastslide.so";
    };
  }

  private static String libSuffix() {
    return switch (osName()) {
      case "windows" -> ".dll";
      case "darwin" -> ".dylib";
      default -> ".so";
    };
  }
}
