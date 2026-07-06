// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide.internal;

import static java.lang.foreign.ValueLayout.*;

import dev.aifo.fastslide.FastSlideException;
import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;

/**
 * FFM (Foreign Function & Memory) bindings to the FastSlide C API.
 *
 * <p>Each C function is mapped to a {@link MethodHandle} obtained via {@link
 * Linker#downcallHandle}. Public static methods provide a typed Java interface and handle error
 * checking via {@code fastslide_get_last_error()}.
 */
public final class FastSlideNative {
  private FastSlideNative() {}

  private static final Linker LINKER = Linker.nativeLinker();
  private static final SymbolLookup LOOKUP = NativeLoader.lookup();

  private static MethodHandle downcall(String name, FunctionDescriptor desc) {
    MemorySegment symbol =
        LOOKUP
            .find(name)
            .orElseThrow(
                () -> new FastSlideException("Symbol not found in native library: " + name));
    return LINKER.downcallHandle(symbol, desc);
  }

  // -----------------------------------------------------------------------
  // Error handling
  // -----------------------------------------------------------------------
  private static final MethodHandle GET_LAST_ERROR =
      downcall("fastslide_get_last_error", FunctionDescriptor.of(ADDRESS));
  private static final MethodHandle CLEAR_LAST_ERROR =
      downcall("fastslide_clear_last_error", FunctionDescriptor.ofVoid());

  static String getLastError() {
    try {
      MemorySegment ptr = (MemorySegment) GET_LAST_ERROR.invokeExact();
      if (ptr.equals(MemorySegment.NULL)) return null;
      return ptr.reinterpret(Long.MAX_VALUE).getString(0);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  static void clearLastError() {
    try {
      CLEAR_LAST_ERROR.invokeExact();
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  private static String lastErrorOr(String fallback) {
    String err = getLastError();
    return (err != null && !err.isEmpty()) ? err : fallback;
  }

  private static void checkSuccess(int rc, String operation) {
    if (rc == 0) {
      throw new FastSlideException(operation + ": " + lastErrorOr("failed"));
    }
  }

  private static MemorySegment checkNonNull(MemorySegment ptr, String operation) {
    if (ptr.equals(MemorySegment.NULL)) {
      throw new FastSlideException(operation + ": " + lastErrorOr("returned null"));
    }
    return ptr;
  }

  private static RuntimeException wrap(Throwable t) {
    if (t instanceof RuntimeException re) return re;
    if (t instanceof Error e) throw e;
    return new FastSlideException("FFM invocation failed", t);
  }

  // -----------------------------------------------------------------------
  // Initialization / version
  // -----------------------------------------------------------------------
  private static final MethodHandle INITIALIZE =
      downcall("fastslide_initialize", FunctionDescriptor.of(JAVA_INT));
  private static final MethodHandle GET_VERSION =
      downcall("fastslide_get_version", FunctionDescriptor.of(ADDRESS));
  private static final MethodHandle C_API_GET_VERSION =
      downcall("fastslide_c_api_get_version", FunctionDescriptor.of(ADDRESS));

  public static void initialize() {
    try {
      clearLastError();
      checkSuccess((int) INITIALIZE.invokeExact(), "fastslide_initialize");
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static String getVersion() {
    try {
      MemorySegment ptr = (MemorySegment) GET_VERSION.invokeExact();
      return checkNonNull(ptr, "fastslide_get_version").reinterpret(Long.MAX_VALUE).getString(0);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static String getCApiVersion() {
    try {
      MemorySegment ptr = (MemorySegment) C_API_GET_VERSION.invokeExact();
      return checkNonNull(ptr, "fastslide_c_api_get_version")
          .reinterpret(Long.MAX_VALUE)
          .getString(0);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  // -----------------------------------------------------------------------
  // Registry: open, extensions, is_supported
  // -----------------------------------------------------------------------
  private static final MethodHandle CREATE_READER =
      downcall("fastslide_create_reader", FunctionDescriptor.of(ADDRESS, ADDRESS));
  private static final MethodHandle CREATE_READER_WITH_OPTIONS =
      downcall(
          "fastslide_create_reader_with_options",
          FunctionDescriptor.of(ADDRESS, ADDRESS, ADDRESS));
  private static final MethodHandle GET_SUPPORTED_EXTENSIONS =
      downcall(
          "fastslide_get_supported_extensions", FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle FREE_EXTENSIONS =
      downcall("fastslide_registry_free_extensions", FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT));
  private static final MethodHandle IS_SUPPORTED =
      downcall("fastslide_is_supported", FunctionDescriptor.of(JAVA_INT, ADDRESS));

  // FastSlideOpenOptions layout: { int apply_icc (0); int target_color_space (4);
  //   int rendering_intent (8); } => 12 bytes (C enums are int-sized).
  static final long OPEN_OPTIONS_SIZE = 12;

  public static MemorySegment createReader(Arena arena, String path) {
    try {
      MemorySegment pathSeg = arena.allocateFrom(path);
      clearLastError();
      MemorySegment reader = (MemorySegment) CREATE_READER.invokeExact(pathSeg);
      return checkNonNull(reader, "fastslide_create_reader");
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static MemorySegment createReaderWithOptions(
      Arena arena, String path, boolean applyIcc, int targetColorSpace, int renderingIntent) {
    try {
      MemorySegment pathSeg = arena.allocateFrom(path);
      MemorySegment options = arena.allocate(OPEN_OPTIONS_SIZE);
      options.set(JAVA_INT, 0, applyIcc ? 1 : 0);
      options.set(JAVA_INT, 4, targetColorSpace);
      options.set(JAVA_INT, 8, renderingIntent);
      clearLastError();
      MemorySegment reader =
          (MemorySegment) CREATE_READER_WITH_OPTIONS.invokeExact(pathSeg, options);
      return checkNonNull(reader, "fastslide_create_reader_with_options");
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static String[] getSupportedExtensions(Arena arena) {
    try {
      MemorySegment pExtensions = arena.allocate(ADDRESS);
      MemorySegment pCount = arena.allocate(JAVA_INT);
      clearLastError();
      checkSuccess(
          (int) GET_SUPPORTED_EXTENSIONS.invokeExact(pExtensions, pCount),
          "fastslide_get_supported_extensions");
      int count = pCount.get(JAVA_INT, 0);
      MemorySegment arrayPtr =
          pExtensions.get(ADDRESS, 0).reinterpret((long) count * ADDRESS.byteSize());
      String[] result = new String[count];
      for (int i = 0; i < count; i++) {
        MemorySegment strPtr = arrayPtr.getAtIndex(ADDRESS, i).reinterpret(Long.MAX_VALUE);
        result[i] = strPtr.getString(0);
      }
      FREE_EXTENSIONS.invokeExact(pExtensions.get(ADDRESS, 0), count);
      return result;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static boolean isSupported(Arena arena, String path) {
    try {
      MemorySegment pathSeg = arena.allocateFrom(path);
      clearLastError();
      return ((int) IS_SUPPORTED.invokeExact(pathSeg)) != 0;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  // -----------------------------------------------------------------------
  // SlideReader
  // -----------------------------------------------------------------------
  private static final MethodHandle READER_FREE =
      downcall("fastslide_slide_reader_free", FunctionDescriptor.ofVoid(ADDRESS));
  private static final MethodHandle READER_GET_LEVEL_COUNT =
      downcall("fastslide_slide_reader_get_level_count", FunctionDescriptor.of(JAVA_INT, ADDRESS));
  private static final MethodHandle READER_GET_BASE_DIMS =
      downcall(
          "fastslide_slide_reader_get_base_dimensions",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle READER_GET_LEVEL_DIMS =
      downcall(
          "fastslide_slide_reader_get_level_dimensions",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS));
  private static final MethodHandle READER_GET_LEVEL_DOWNSAMPLE =
      downcall(
          "fastslide_slide_reader_get_level_downsample",
          FunctionDescriptor.of(JAVA_DOUBLE, ADDRESS, JAVA_INT));
  private static final MethodHandle READER_GET_BEST_LEVEL =
      downcall(
          "fastslide_slide_reader_get_best_level_for_downsample",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_DOUBLE));
  private static final MethodHandle READER_GET_FORMAT_NAME =
      downcall("fastslide_slide_reader_get_format_name", FunctionDescriptor.of(ADDRESS, ADDRESS));
  private static final MethodHandle READER_GET_IMAGE_FORMAT =
      downcall("fastslide_slide_reader_get_image_format", FunctionDescriptor.of(JAVA_INT, ADDRESS));
  private static final MethodHandle READER_GET_TILE_SIZE =
      downcall(
          "fastslide_slide_reader_get_tile_size",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle READER_GET_PROPERTIES =
      downcall(
          "fastslide_slide_reader_get_properties",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle READER_FREE_PROPERTIES =
      downcall("fastslide_slide_reader_free_properties", FunctionDescriptor.ofVoid(ADDRESS));
  private static final MethodHandle READER_GET_ASSOC_NAMES =
      downcall(
          "fastslide_slide_reader_get_associated_image_names",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
  private static final MethodHandle READER_FREE_ASSOC_NAMES =
      downcall(
          "fastslide_slide_reader_free_associated_image_names",
          FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT));
  private static final MethodHandle READER_GET_ASSOC_DIMS =
      downcall(
          "fastslide_slide_reader_get_associated_image_dimensions",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
  private static final MethodHandle READER_READ_REGION_COORDS =
      downcall(
          "fastslide_slide_reader_read_region_coords",
          FunctionDescriptor.of(
              ADDRESS, ADDRESS, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT,
              JAVA_INT));
  private static final MethodHandle READER_GET_STACK_INFO =
      downcall(
          "fastslide_slide_reader_get_stack_info",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle READER_GET_ICC_PROFILE_SIZE =
      downcall(
          "fastslide_slide_reader_get_icc_profile_size",
          FunctionDescriptor.of(JAVA_LONG, ADDRESS));
  private static final MethodHandle READER_READ_ICC_PROFILE =
      downcall(
          "fastslide_slide_reader_read_icc_profile",
          FunctionDescriptor.of(JAVA_LONG, ADDRESS, ADDRESS, JAVA_LONG));
  private static final MethodHandle READER_ENABLE_ICC_TRANSFORM =
      downcall(
          "fastslide_slide_reader_enable_icc_transform",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, JAVA_INT));

  // FastSlideImageDimensions layout: { uint32_t width; uint32_t height; }
  static final long DIMS_SIZE = 8;

  // FastSlideSlideProperties layout (on 64-bit):
  //   double mpp_x (0), double mpp_y (8), double objective_magnification (16),
  //   char* objective_name (24), char* scanner_model (32), char* scan_date (40)
  static final long PROPS_SIZE = 48;

  // FastSlideStackInfo layout (doubles are 8-byte aligned):
  //   uint32_t z_count (0), uint32_t t_count (4), int has_z_spacing (8),
  //   [4 pad], double z_spacing_um (16), int has_t_interval (24),
  //   [4 pad], double t_interval_s (32) => 40 bytes total
  static final long STACK_INFO_SIZE = 40;

  public static void readerFree(MemorySegment reader) {
    try {
      READER_FREE.invokeExact(reader);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int readerGetLevelCount(MemorySegment reader) {
    try {
      clearLastError();
      int count = (int) READER_GET_LEVEL_COUNT.invokeExact(reader);
      if (count < 0)
        throw new FastSlideException(
            "fastslide_slide_reader_get_level_count: " + lastErrorOr("failed"));
      return count;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int[] readerGetBaseDimensions(Arena arena, MemorySegment reader) {
    try {
      MemorySegment dims = arena.allocate(DIMS_SIZE);
      clearLastError();
      checkSuccess(
          (int) READER_GET_BASE_DIMS.invokeExact(reader, dims),
          "fastslide_slide_reader_get_base_dimensions");
      return new int[] {dims.get(JAVA_INT, 0), dims.get(JAVA_INT, 4)};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int[] readerGetLevelDimensions(Arena arena, MemorySegment reader, int level) {
    try {
      MemorySegment dims = arena.allocate(DIMS_SIZE);
      clearLastError();
      checkSuccess(
          (int) READER_GET_LEVEL_DIMS.invokeExact(reader, level, dims),
          "fastslide_slide_reader_get_level_dimensions");
      return new int[] {dims.get(JAVA_INT, 0), dims.get(JAVA_INT, 4)};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static double readerGetLevelDownsample(MemorySegment reader, int level) {
    try {
      clearLastError();
      double d = (double) READER_GET_LEVEL_DOWNSAMPLE.invokeExact(reader, level);
      if (d < 0.0)
        throw new FastSlideException(
            "fastslide_slide_reader_get_level_downsample: " + lastErrorOr("failed"));
      return d;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int readerGetBestLevelForDownsample(MemorySegment reader, double downsample) {
    try {
      clearLastError();
      int level = (int) READER_GET_BEST_LEVEL.invokeExact(reader, downsample);
      if (level < 0)
        throw new FastSlideException(
            "fastslide_slide_reader_get_best_level_for_downsample: " + lastErrorOr("failed"));
      return level;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static String readerGetFormatName(MemorySegment reader) {
    try {
      clearLastError();
      MemorySegment ptr = (MemorySegment) READER_GET_FORMAT_NAME.invokeExact(reader);
      return checkNonNull(ptr, "fastslide_slide_reader_get_format_name")
          .reinterpret(Long.MAX_VALUE)
          .getString(0);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int readerGetImageFormat(MemorySegment reader) {
    try {
      clearLastError();
      return (int) READER_GET_IMAGE_FORMAT.invokeExact(reader);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int[] readerGetTileSize(Arena arena, MemorySegment reader) {
    try {
      MemorySegment dims = arena.allocate(DIMS_SIZE);
      clearLastError();
      checkSuccess(
          (int) READER_GET_TILE_SIZE.invokeExact(reader, dims),
          "fastslide_slide_reader_get_tile_size");
      return new int[] {dims.get(JAVA_INT, 0), dims.get(JAVA_INT, 4)};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  /** Returns [mpp_x, mpp_y, objective_magnification, objective_name, scanner_model, scan_date]. */
  public static Object[] readerGetProperties(Arena arena, MemorySegment reader) {
    try {
      MemorySegment props = arena.allocate(PROPS_SIZE);
      clearLastError();
      checkSuccess(
          (int) READER_GET_PROPERTIES.invokeExact(reader, props),
          "fastslide_slide_reader_get_properties");

      double mppX = props.get(JAVA_DOUBLE, 0);
      double mppY = props.get(JAVA_DOUBLE, 8);
      double magnification = props.get(JAVA_DOUBLE, 16);

      String objectiveName = readNullableString(props.get(ADDRESS, 24));
      String scannerModel = readNullableString(props.get(ADDRESS, 32));
      String scanDate = readNullableString(props.get(ADDRESS, 40));

      READER_FREE_PROPERTIES.invokeExact(props);
      return new Object[] {mppX, mppY, magnification, objectiveName, scannerModel, scanDate};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static String[] readerGetAssociatedImageNames(Arena arena, MemorySegment reader) {
    try {
      MemorySegment pNames = arena.allocate(ADDRESS);
      MemorySegment pCount = arena.allocate(JAVA_INT);
      clearLastError();
      checkSuccess(
          (int) READER_GET_ASSOC_NAMES.invokeExact(reader, pNames, pCount),
          "fastslide_slide_reader_get_associated_image_names");
      int count = pCount.get(JAVA_INT, 0);
      MemorySegment arrayPtr =
          pNames.get(ADDRESS, 0).reinterpret((long) count * ADDRESS.byteSize());
      String[] result = new String[count];
      for (int i = 0; i < count; i++) {
        result[i] = arrayPtr.getAtIndex(ADDRESS, i).reinterpret(Long.MAX_VALUE).getString(0);
      }
      READER_FREE_ASSOC_NAMES.invokeExact(pNames.get(ADDRESS, 0), count);
      return result;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int[] readerGetAssociatedImageDimensions(
      Arena arena, MemorySegment reader, String name) {
    try {
      MemorySegment nameSeg = arena.allocateFrom(name);
      MemorySegment dims = arena.allocate(DIMS_SIZE);
      clearLastError();
      checkSuccess(
          (int) READER_GET_ASSOC_DIMS.invokeExact(reader, nameSeg, dims),
          "fastslide_slide_reader_get_associated_image_dimensions");
      return new int[] {dims.get(JAVA_INT, 0), dims.get(JAVA_INT, 4)};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static MemorySegment readerReadRegionCoords(
      MemorySegment reader, int x, int y, int w, int h, int level, int z, int t) {
    try {
      clearLastError();
      MemorySegment img =
          (MemorySegment) READER_READ_REGION_COORDS.invokeExact(reader, x, y, w, h, level, z, t);
      return checkNonNull(img, "fastslide_slide_reader_read_region_coords");
    } catch (Throwable t2) {
      throw wrap(t2);
    }
  }

  /** Returns [zCount, tCount, hasZSpacing, zSpacingUm, hasTInterval, tIntervalS]. */
  public static Object[] readerGetStackInfo(Arena arena, MemorySegment reader) {
    try {
      MemorySegment info = arena.allocate(STACK_INFO_SIZE);
      clearLastError();
      checkSuccess(
          (int) READER_GET_STACK_INFO.invokeExact(reader, info),
          "fastslide_slide_reader_get_stack_info");
      return parseStackInfo(info);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  /** Copies the embedded ICC profile bytes, or {@code null} if the slide has none. */
  public static byte[] readerReadIccProfile(Arena arena, MemorySegment reader) {
    try {
      clearLastError();
      long size = (long) READER_GET_ICC_PROFILE_SIZE.invokeExact(reader);
      if (size == 0) {
        return null;
      }
      MemorySegment buffer = arena.allocate(size);
      long written = (long) READER_READ_ICC_PROFILE.invokeExact(reader, buffer, size);
      if (written == 0) {
        throw new FastSlideException(
            "fastslide_slide_reader_read_icc_profile: " + lastErrorOr("failed"));
      }
      return buffer.asSlice(0, written).toArray(JAVA_BYTE);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static void readerEnableIccTransform(
      MemorySegment reader, int targetColorSpace, int renderingIntent) {
    try {
      clearLastError();
      checkSuccess(
          (int) READER_ENABLE_ICC_TRANSFORM.invokeExact(reader, targetColorSpace, renderingIntent),
          "fastslide_slide_reader_enable_icc_transform");
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  // -----------------------------------------------------------------------
  // Channel metadata
  // -----------------------------------------------------------------------
  private static final MethodHandle READER_GET_CHANNEL_METADATA =
      downcall(
          "fastslide_slide_reader_get_channel_metadata",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
  private static final MethodHandle READER_FREE_CHANNEL_METADATA =
      downcall(
          "fastslide_slide_reader_free_channel_metadata",
          FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT));

  // FastSlideChannelMetadata struct layout (64-bit):
  //   char* name (0), char* biomarker (8),
  //   {uint8 r, g, b} color (16..18), [1 pad],
  //   uint32 exposure_time (20), uint32 signal_units (24),
  //   [4 pad] => 32 bytes total
  static final long CHANNEL_META_SIZE = 32;

  /**
   * Returns channel metadata as Object[][count], each row: [String name, String biomarker, int r,
   * int g, int b, int exposureTime, int signalUnits]
   */
  public static Object[][] readerGetChannelMetadata(Arena arena, MemorySegment reader) {
    try {
      MemorySegment pMeta = arena.allocate(ADDRESS);
      MemorySegment pCount = arena.allocate(JAVA_INT);
      clearLastError();
      checkSuccess(
          (int) READER_GET_CHANNEL_METADATA.invokeExact(reader, pMeta, pCount),
          "fastslide_slide_reader_get_channel_metadata");
      int count = pCount.get(JAVA_INT, 0);
      MemorySegment arrayBase = pMeta.get(ADDRESS, 0).reinterpret(count * CHANNEL_META_SIZE);
      Object[][] result = new Object[count][];
      for (int i = 0; i < count; i++) {
        long base = i * CHANNEL_META_SIZE;
        String name = readNullableString(arrayBase.get(ADDRESS, base));
        String biomarker = readNullableString(arrayBase.get(ADDRESS, base + 8));
        int r = Byte.toUnsignedInt(arrayBase.get(JAVA_BYTE, base + 16));
        int g = Byte.toUnsignedInt(arrayBase.get(JAVA_BYTE, base + 17));
        int b = Byte.toUnsignedInt(arrayBase.get(JAVA_BYTE, base + 18));
        int exposureTime = arrayBase.get(JAVA_INT, base + 20);
        int signalUnits = arrayBase.get(JAVA_INT, base + 24);
        result[i] = new Object[] {name, biomarker, r, g, b, exposureTime, signalUnits};
      }
      READER_FREE_CHANNEL_METADATA.invokeExact(pMeta.get(ADDRESS, 0), count);
      return result;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  // -----------------------------------------------------------------------
  // Multi-image container API
  // -----------------------------------------------------------------------
  private static final MethodHandle READER_GET_IMAGE_COUNT =
      downcall("fastslide_slide_reader_get_image_count", FunctionDescriptor.of(JAVA_INT, ADDRESS));
  private static final MethodHandle READER_GET_PRIMARY_IMAGE_INDEX =
      downcall(
          "fastslide_slide_reader_get_primary_image_index",
          FunctionDescriptor.of(JAVA_INT, ADDRESS));
  private static final MethodHandle READER_GET_IMAGE_NAMES =
      downcall(
          "fastslide_slide_reader_get_image_names",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
  private static final MethodHandle READER_FREE_IMAGE_NAMES =
      downcall(
          "fastslide_slide_reader_free_image_names", FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT));
  private static final MethodHandle READER_GET_IMAGE =
      downcall(
          "fastslide_slide_reader_get_image", FunctionDescriptor.of(ADDRESS, ADDRESS, JAVA_INT));

  public static int readerGetImageCount(MemorySegment reader) {
    try {
      clearLastError();
      int count = (int) READER_GET_IMAGE_COUNT.invokeExact(reader);
      if (count < 0)
        throw new FastSlideException(
            "fastslide_slide_reader_get_image_count: " + lastErrorOr("failed"));
      return count;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int readerGetPrimaryImageIndex(MemorySegment reader) {
    try {
      clearLastError();
      int index = (int) READER_GET_PRIMARY_IMAGE_INDEX.invokeExact(reader);
      if (index < 0)
        throw new FastSlideException(
            "fastslide_slide_reader_get_primary_image_index: " + lastErrorOr("failed"));
      return index;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static String[] readerGetImageNames(Arena arena, MemorySegment reader) {
    try {
      MemorySegment pNames = arena.allocate(ADDRESS);
      MemorySegment pCount = arena.allocate(JAVA_INT);
      clearLastError();
      checkSuccess(
          (int) READER_GET_IMAGE_NAMES.invokeExact(reader, pNames, pCount),
          "fastslide_slide_reader_get_image_names");
      int count = pCount.get(JAVA_INT, 0);
      MemorySegment arrayPtr =
          pNames.get(ADDRESS, 0).reinterpret((long) count * ADDRESS.byteSize());
      String[] result = new String[count];
      for (int i = 0; i < count; i++) {
        result[i] = arrayPtr.getAtIndex(ADDRESS, i).reinterpret(Long.MAX_VALUE).getString(0);
      }
      READER_FREE_IMAGE_NAMES.invokeExact(pNames.get(ADDRESS, 0), count);
      return result;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static MemorySegment readerGetImage(MemorySegment reader, int index) {
    try {
      clearLastError();
      MemorySegment image = (MemorySegment) READER_GET_IMAGE.invokeExact(reader, index);
      return checkNonNull(image, "fastslide_slide_reader_get_image");
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  // -----------------------------------------------------------------------
  // Per-image (per-series) API
  // -----------------------------------------------------------------------
  private static final MethodHandle SLIDE_IMAGE_FREE =
      downcall("fastslide_slide_image_free", FunctionDescriptor.ofVoid(ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_LEVEL_COUNT =
      downcall("fastslide_slide_image_get_level_count", FunctionDescriptor.of(JAVA_INT, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_LEVEL_DIMS =
      downcall(
          "fastslide_slide_image_get_level_dimensions",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_LEVEL_DOWNSAMPLE =
      downcall(
          "fastslide_slide_image_get_level_downsample",
          FunctionDescriptor.of(JAVA_DOUBLE, ADDRESS, JAVA_INT));
  private static final MethodHandle SLIDE_IMAGE_GET_BASE_DIMS =
      downcall(
          "fastslide_slide_image_get_base_dimensions",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_TILE_SIZE =
      downcall(
          "fastslide_slide_image_get_tile_size", FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_IMAGE_FORMAT =
      downcall("fastslide_slide_image_get_image_format", FunctionDescriptor.of(JAVA_INT, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_DATA_TYPE =
      downcall("fastslide_slide_image_get_data_type", FunctionDescriptor.of(JAVA_INT, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_CHANNEL_METADATA =
      downcall(
          "fastslide_slide_image_get_channel_metadata",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_GET_PROPERTIES =
      downcall(
          "fastslide_slide_image_get_properties",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle SLIDE_IMAGE_READ_REGION_COORDS =
      downcall(
          "fastslide_slide_image_read_region_coords",
          FunctionDescriptor.of(
              ADDRESS, ADDRESS, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT,
              JAVA_INT));
  private static final MethodHandle SLIDE_IMAGE_GET_STACK_INFO =
      downcall(
          "fastslide_slide_image_get_stack_info",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));

  public static void slideImageFree(MemorySegment image) {
    try {
      SLIDE_IMAGE_FREE.invokeExact(image);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int slideImageGetLevelCount(MemorySegment image) {
    try {
      clearLastError();
      int count = (int) SLIDE_IMAGE_GET_LEVEL_COUNT.invokeExact(image);
      if (count < 0)
        throw new FastSlideException(
            "fastslide_slide_image_get_level_count: " + lastErrorOr("failed"));
      return count;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int[] slideImageGetLevelDimensions(Arena arena, MemorySegment image, int level) {
    try {
      MemorySegment dims = arena.allocate(DIMS_SIZE);
      clearLastError();
      checkSuccess(
          (int) SLIDE_IMAGE_GET_LEVEL_DIMS.invokeExact(image, level, dims),
          "fastslide_slide_image_get_level_dimensions");
      return new int[] {dims.get(JAVA_INT, 0), dims.get(JAVA_INT, 4)};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static double slideImageGetLevelDownsample(MemorySegment image, int level) {
    try {
      clearLastError();
      double d = (double) SLIDE_IMAGE_GET_LEVEL_DOWNSAMPLE.invokeExact(image, level);
      if (d < 0.0)
        throw new FastSlideException(
            "fastslide_slide_image_get_level_downsample: " + lastErrorOr("failed"));
      return d;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int[] slideImageGetBaseDimensions(Arena arena, MemorySegment image) {
    try {
      MemorySegment dims = arena.allocate(DIMS_SIZE);
      clearLastError();
      checkSuccess(
          (int) SLIDE_IMAGE_GET_BASE_DIMS.invokeExact(image, dims),
          "fastslide_slide_image_get_base_dimensions");
      return new int[] {dims.get(JAVA_INT, 0), dims.get(JAVA_INT, 4)};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int[] slideImageGetTileSize(Arena arena, MemorySegment image) {
    try {
      MemorySegment dims = arena.allocate(DIMS_SIZE);
      clearLastError();
      checkSuccess(
          (int) SLIDE_IMAGE_GET_TILE_SIZE.invokeExact(image, dims),
          "fastslide_slide_image_get_tile_size");
      return new int[] {dims.get(JAVA_INT, 0), dims.get(JAVA_INT, 4)};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int slideImageGetImageFormat(MemorySegment image) {
    try {
      clearLastError();
      return (int) SLIDE_IMAGE_GET_IMAGE_FORMAT.invokeExact(image);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static int slideImageGetDataType(MemorySegment image) {
    try {
      clearLastError();
      return (int) SLIDE_IMAGE_GET_DATA_TYPE.invokeExact(image);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  /**
   * Returns channel metadata as Object[][count], each row: [String name, String biomarker, int r,
   * int g, int b, int exposureTime, int signalUnits]
   */
  public static Object[][] slideImageGetChannelMetadata(Arena arena, MemorySegment image) {
    try {
      MemorySegment pMeta = arena.allocate(ADDRESS);
      MemorySegment pCount = arena.allocate(JAVA_INT);
      clearLastError();
      checkSuccess(
          (int) SLIDE_IMAGE_GET_CHANNEL_METADATA.invokeExact(image, pMeta, pCount),
          "fastslide_slide_image_get_channel_metadata");
      int count = pCount.get(JAVA_INT, 0);
      MemorySegment arrayBase = pMeta.get(ADDRESS, 0).reinterpret(count * CHANNEL_META_SIZE);
      Object[][] result = new Object[count][];
      for (int i = 0; i < count; i++) {
        long base = i * CHANNEL_META_SIZE;
        String name = readNullableString(arrayBase.get(ADDRESS, base));
        String biomarker = readNullableString(arrayBase.get(ADDRESS, base + 8));
        int r = Byte.toUnsignedInt(arrayBase.get(JAVA_BYTE, base + 16));
        int g = Byte.toUnsignedInt(arrayBase.get(JAVA_BYTE, base + 17));
        int b = Byte.toUnsignedInt(arrayBase.get(JAVA_BYTE, base + 18));
        int exposureTime = arrayBase.get(JAVA_INT, base + 20);
        int signalUnits = arrayBase.get(JAVA_INT, base + 24);
        result[i] = new Object[] {name, biomarker, r, g, b, exposureTime, signalUnits};
      }
      // Shared free helper with the reader-level channel metadata.
      READER_FREE_CHANNEL_METADATA.invokeExact(pMeta.get(ADDRESS, 0), count);
      return result;
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  /** Returns [mpp_x, mpp_y, objective_magnification, objective_name, scanner_model, scan_date]. */
  public static Object[] slideImageGetProperties(Arena arena, MemorySegment image) {
    try {
      MemorySegment props = arena.allocate(PROPS_SIZE);
      clearLastError();
      checkSuccess(
          (int) SLIDE_IMAGE_GET_PROPERTIES.invokeExact(image, props),
          "fastslide_slide_image_get_properties");

      double mppX = props.get(JAVA_DOUBLE, 0);
      double mppY = props.get(JAVA_DOUBLE, 8);
      double magnification = props.get(JAVA_DOUBLE, 16);

      String objectiveName = readNullableString(props.get(ADDRESS, 24));
      String scannerModel = readNullableString(props.get(ADDRESS, 32));
      String scanDate = readNullableString(props.get(ADDRESS, 40));

      // Shared free helper with the reader-level properties.
      READER_FREE_PROPERTIES.invokeExact(props);
      return new Object[] {mppX, mppY, magnification, objectiveName, scannerModel, scanDate};
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static MemorySegment slideImageReadRegionCoords(
      MemorySegment image, int x, int y, int w, int h, int level, int z, int t) {
    try {
      clearLastError();
      MemorySegment img =
          (MemorySegment)
              SLIDE_IMAGE_READ_REGION_COORDS.invokeExact(image, x, y, w, h, level, z, t);
      return checkNonNull(img, "fastslide_slide_image_read_region_coords");
    } catch (Throwable t2) {
      throw wrap(t2);
    }
  }

  /** Returns [zCount, tCount, hasZSpacing, zSpacingUm, hasTInterval, tIntervalS]. */
  public static Object[] slideImageGetStackInfo(Arena arena, MemorySegment image) {
    try {
      MemorySegment info = arena.allocate(STACK_INFO_SIZE);
      clearLastError();
      checkSuccess(
          (int) SLIDE_IMAGE_GET_STACK_INFO.invokeExact(image, info),
          "fastslide_slide_image_get_stack_info");
      return parseStackInfo(info);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  // -----------------------------------------------------------------------
  // Image
  // -----------------------------------------------------------------------
  private static final MethodHandle IMAGE_FREE =
      downcall("fastslide_image_free", FunctionDescriptor.ofVoid(ADDRESS));
  private static final MethodHandle IMAGE_GET_INFO =
      downcall("fastslide_image_get_info", FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS));
  private static final MethodHandle IMAGE_GET_SIZE_BYTES =
      downcall("fastslide_image_get_size_bytes", FunctionDescriptor.of(JAVA_LONG, ADDRESS));
  private static final MethodHandle IMAGE_COPY_DATA =
      downcall(
          "fastslide_image_copy_data",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, JAVA_LONG));

  // FastSlideImageInfo layout (on 64-bit):
  //   int format (0), int data_type (4), int planar_config (8),
  //   uint32_t width (12), uint32_t height (16), uint32_t channels (20),
  //   [4 bytes padding] (24),
  //   size_t bytes_per_sample (28->32 with alignment), size_t data_size
  // Actual layout depends on alignment. Let's use the struct size approach:
  // enums are int (4), uint32_t (4), size_t (8) on 64-bit.
  // {int,int,int, u32,u32,u32, size_t,size_t}
  // = {4,4,4, 4,4,4, 8,8} = 24 + 16 = 40 BUT alignment means size_t at offset 24:
  // offset 0: format(4), 4: data_type(4), 8: planar_config(4),
  // 12: width(4), 16: height(4), 20: channels(4),
  // 24: bytes_per_sample(8), 32: data_size(8) => total 40
  static final long IMAGE_INFO_SIZE = 40;

  public static void imageFree(MemorySegment image) {
    try {
      IMAGE_FREE.invokeExact(image);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  /**
   * Returns packed info: [format, dataType, planarConfig, width, height, channels, bytesPerSample,
   * dataSize]
   */
  public static long[] imageGetInfo(Arena arena, MemorySegment image) {
    try {
      MemorySegment info = arena.allocate(IMAGE_INFO_SIZE);
      clearLastError();
      checkSuccess((int) IMAGE_GET_INFO.invokeExact(image, info), "fastslide_image_get_info");
      return new long[] {
        info.get(JAVA_INT, 0), // format
        info.get(JAVA_INT, 4), // data_type
        info.get(JAVA_INT, 8), // planar_config
        Integer.toUnsignedLong(info.get(JAVA_INT, 12)), // width
        Integer.toUnsignedLong(info.get(JAVA_INT, 16)), // height
        Integer.toUnsignedLong(info.get(JAVA_INT, 20)), // channels
        info.get(JAVA_LONG, 24), // bytes_per_sample
        info.get(JAVA_LONG, 32), // data_size
      };
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static long imageGetSizeBytes(MemorySegment image) {
    try {
      clearLastError();
      return (long) IMAGE_GET_SIZE_BYTES.invokeExact(image);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  public static byte[] imageCopyData(Arena arena, MemorySegment image) {
    try {
      long size = imageGetSizeBytes(image);
      if (size == 0) {
        String err = getLastError();
        if (err != null && !err.isEmpty())
          throw new FastSlideException("fastslide_image_get_size_bytes: " + err);
        return new byte[0];
      }
      MemorySegment buffer = arena.allocate(size);
      clearLastError();
      checkSuccess(
          (int) IMAGE_COPY_DATA.invokeExact(image, buffer, size), "fastslide_image_copy_data");
      return buffer.toArray(JAVA_BYTE);
    } catch (Throwable t) {
      throw wrap(t);
    }
  }

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------
  private static String readNullableString(MemorySegment ptr) {
    if (ptr.equals(MemorySegment.NULL)) return "";
    return ptr.reinterpret(Long.MAX_VALUE).getString(0);
  }

  /**
   * Decodes a FastSlideStackInfo struct into [zCount, tCount, hasZSpacing, zSpacingUm,
   * hasTInterval, tIntervalS].
   */
  private static Object[] parseStackInfo(MemorySegment info) {
    int zCount = info.get(JAVA_INT, 0);
    int tCount = info.get(JAVA_INT, 4);
    boolean hasZSpacing = info.get(JAVA_INT, 8) != 0;
    double zSpacingUm = info.get(JAVA_DOUBLE, 16);
    boolean hasTInterval = info.get(JAVA_INT, 24) != 0;
    double tIntervalS = info.get(JAVA_DOUBLE, 32);
    return new Object[] {zCount, tCount, hasZSpacing, zSpacingUm, hasTInterval, tIntervalS};
  }
}
