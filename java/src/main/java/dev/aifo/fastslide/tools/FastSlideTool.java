// Copyright 2026 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

package dev.aifo.fastslide.tools;

import dev.aifo.fastslide.Dimensions;
import dev.aifo.fastslide.FastSlide;
import dev.aifo.fastslide.Image;
import dev.aifo.fastslide.ImageFormat;
import dev.aifo.fastslide.ImageInfo;
import dev.aifo.fastslide.SlideProperties;
import dev.aifo.fastslide.SlideReader;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import javax.imageio.ImageIO;

/**
 * Java CLI equivalent of {@code aifo/fastslide/tools/fastslidetool.cpp} (core subset).
 *
 * <p>Supported subcommands:
 *
 * <ul>
 *   <li>{@code info --input <path>}
 *   <li>{@code region --input <path> --x <int> --y <int> --width <int> --height <int> --level <int>
 *       --output <png>}
 * </ul>
 */
public final class FastSlideTool {
  private FastSlideTool() {}

  public static void main(String[] args) throws Exception {
    if (args.length == 0 || "--help".equals(args[0]) || "-h".equals(args[0])) {
      printUsageAndExit(0);
      return;
    }

    String cmd = args[0];
    List<String> rest = new ArrayList<>();
    for (int i = 1; i < args.length; ++i) {
      rest.add(args[i]);
    }

    switch (cmd) {
      case "info":
        runInfo(parseArgs(rest));
        break;
      case "region":
        runRegion(parseArgs(rest));
        break;
      default:
        System.err.println("Unknown command: " + cmd);
        printUsageAndExit(2);
    }
  }

  private static final int KV_WIDTH = 30;

  private static void printSeparator(char c) {
    System.out.println(String.valueOf(c).repeat(80));
  }

  private static void printHeader(String title) {
    System.out.println();
    printSeparator('=');
    System.out.println(" " + title);
    printSeparator('=');
  }

  private static void printSubHeader(String title) {
    System.out.println();
    System.out.println("--- " + title + " ---");
  }

  private static void printKeyValue(String key, String value, int width) {
    System.out.printf("%-" + width + "s%s%n", key + ":", value);
  }

  private static void printKeyValue(String key, String value) {
    printKeyValue(key, value, KV_WIDTH);
  }

  private static void printKeyValue(String key, double value) {
    System.out.printf("%-" + KV_WIDTH + "s%.6f%n", key + ":", value);
  }

  private static void printKeyValue(String key, int value) {
    System.out.printf("%-" + KV_WIDTH + "s%d%n", key + ":", value);
  }

  private static void runInfo(Args a) {
    String input = require(a.getString("input"), "--input is required");
    boolean verbose = a.getString("verbose") != null;

    System.out.println("Opening slide: " + input);

    try (SlideReader reader = FastSlide.open(input)) {
      // Slide Information
      printHeader("Slide Information");

      printKeyValue("Format", reader.getFormatName());
      printKeyValue("Image Format", reader.getImageFormat().name());

      SlideProperties props = reader.getProperties();
      printKeyValue("MPP X", props.mppX());
      printKeyValue("MPP Y", props.mppY());
      printKeyValue("Objective Magnification", props.objectiveMagnification());

      if (!props.objectiveName().isEmpty()) {
        printKeyValue("Objective Name", props.objectiveName());
      }
      if (!props.scannerModel().isEmpty()) {
        printKeyValue("Scanner Model", props.scannerModel());
      }
      if (!props.scanDate().isEmpty()) {
        printKeyValue("Scan Date", props.scanDate());
      }

      Dimensions tileSize = reader.getTileSize();
      printKeyValue("Tile Size", tileSize.width() + " x " + tileSize.height());

      // Pyramid Levels
      int levelCount = reader.getLevelCount();
      printHeader("Pyramid Levels");
      printKeyValue("Number of Levels", levelCount);

      for (int level = 0; level < levelCount; ++level) {
        Dimensions dims = reader.getLevelDimensions(level);
        double downsample = reader.getLevelDownsample(level);
        printSubHeader("Level " + level);

        printKeyValue("  Dimensions", dims.width() + " x " + dims.height(), 25);
        System.out.printf("%-25s%.6f%n", "  Downsample Factor:", downsample);

        double levelMppX = props.mppX() * downsample;
        double levelMppY = props.mppY() * downsample;
        printKeyValue("  Approx MPP", String.format("%.6f x %.6f", levelMppX, levelMppY), 25);

        double widthMm = dims.width() * levelMppX / 1000.0;
        double heightMm = dims.height() * levelMppY / 1000.0;
        printKeyValue("  Physical Size", String.format("%.6f x %.6f mm", widthMm, heightMm), 25);
      }

      // Associated Images
      String[] assocNames = reader.getAssociatedImageNames();
      if (assocNames.length > 0) {
        printHeader("Associated Images");
        printKeyValue("Number of Images", assocNames.length);

        for (String name : assocNames) {
          try {
            Dimensions dims = reader.getAssociatedImageDimensions(name);
            printKeyValue("  " + name, dims.width() + " x " + dims.height(), 25);
          } catch (Exception e) {
            printKeyValue("  " + name, "unknown size", 25);
          }
        }
      }

      System.out.println();
      printSeparator('=');
      System.out.println("Successfully read slide information!");
      printSeparator('=');
      System.out.println();
    }
  }

  private static void runRegion(Args a) throws IOException {
    String input = require(a.getString("input"), "--input is required");
    int x = a.getIntOr("x", 0);
    int y = a.getIntOr("y", 0);
    int width = a.getIntOr("width", 512);
    int height = a.getIntOr("height", 512);
    int level = a.getIntOr("level", 0);
    String output = a.getStringOr("output", "output.png");

    if (!output.toLowerCase().endsWith(".png")) {
      throw new IllegalArgumentException("--output must end with .png (for now)");
    }

    System.out.println("Opening slide: " + input);
    try (SlideReader reader = FastSlide.open(input)) {
      System.out.println("Reading region:");
      System.out.println("  Position: (" + x + ", " + y + ")");
      System.out.println("  Size: " + width + " x " + height);
      System.out.println("  Level: " + level);
      System.out.println("  Format: " + reader.getFormatName());

      try (Image image = reader.readRegion(x, y, width, height, level)) {
        ImageInfo info = image.getInfo();
        byte[] data = image.copyData();

        BufferedImage buffered = toBufferedImageOrThrow(info, data);
        Path outPath = new File(output).toPath().toAbsolutePath();
        ImageIO.write(buffered, "png", outPath.toFile());
        System.out.println("Wrote: " + outPath);
      }
    }
  }

  private static BufferedImage toBufferedImageOrThrow(ImageInfo info, byte[] data) {
    if (info.dataType() != dev.aifo.fastslide.DataType.UINT8) {
      throw new IllegalArgumentException(
          "Only UINT8 images can be written as PNG (got " + info.dataType() + ")");
    }
    if (info.planarConfig() != dev.aifo.fastslide.PlanarConfig.CONTIG) {
      throw new IllegalArgumentException(
          "Only CONTIG images can be written as PNG (got " + info.planarConfig() + ")");
    }
    int w = info.width();
    int h = info.height();
    if (w <= 0 || h <= 0) {
      throw new IllegalArgumentException("Invalid image dimensions: " + w + "x" + h);
    }

    ImageFormat fmt = info.format();
    switch (fmt) {
      case RGB:
        return rgbToBufferedImage(w, h, data);
      case RGBA:
        return rgbaToBufferedImage(w, h, data);
      default:
        throw new IllegalArgumentException("Unsupported image format for PNG writing: " + fmt);
    }
  }

  private static BufferedImage rgbToBufferedImage(int w, int h, byte[] rgb) {
    int expected = w * h * 3;
    if (rgb.length != expected) {
      throw new IllegalArgumentException(
          "RGB byte[] size mismatch: expected " + expected + ", got " + rgb.length);
    }

    BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
    int idx = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        int r = rgb[idx++] & 0xFF;
        int g = rgb[idx++] & 0xFF;
        int b = rgb[idx++] & 0xFF;
        int argb = (0xFF << 24) | (r << 16) | (g << 8) | b;
        img.setRGB(x, y, argb);
      }
    }
    return img;
  }

  private static BufferedImage rgbaToBufferedImage(int w, int h, byte[] rgba) {
    int expected = w * h * 4;
    if (rgba.length != expected) {
      throw new IllegalArgumentException(
          "RGBA byte[] size mismatch: expected " + expected + ", got " + rgba.length);
    }

    BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
    int idx = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        int r = rgba[idx++] & 0xFF;
        int g = rgba[idx++] & 0xFF;
        int b = rgba[idx++] & 0xFF;
        int a = rgba[idx++] & 0xFF;
        int argb = (a << 24) | (r << 16) | (g << 8) | b;
        img.setRGB(x, y, argb);
      }
    }
    return img;
  }

  private static void printUsageAndExit(int code) {
    System.out.println("FastSlideTool (Java)");
    System.out.println();
    System.out.println("Usage:");
    System.out.println("  fastslidetool_java info --input <slide> [--verbose]");
    System.out.println(
        "  fastslidetool_java region --input <slide> --x <int> --y <int> --width <int> --height"
            + " <int> --level <int> --output <png>");
    System.out.println();
    System.out.println("Notes:");
    System.out.println("  - This is a Java version of core functionality from fastslidetool.cpp.");
    System.out.println(
        "  - Region output currently supports PNG only, and RGB/RGBA uint8 contiguous images.");
    System.exit(code);
  }

  private static String require(String v, String msg) {
    if (v == null || v.isEmpty()) {
      throw new IllegalArgumentException(msg);
    }
    return v;
  }

  private static Args parseArgs(List<String> argv) {
    Args a = new Args();
    for (int i = 0; i < argv.size(); ++i) {
      String tok = argv.get(i);
      if (!tok.startsWith("--")) {
        throw new IllegalArgumentException("Unexpected token: " + tok);
      }
      String key = tok.substring(2);
      if (key.isEmpty()) {
        throw new IllegalArgumentException("Invalid flag: " + tok);
      }
      if (key.equals("verbose")) {
        a.put(key, "true");
        continue;
      }
      if (i + 1 >= argv.size()) {
        throw new IllegalArgumentException("Missing value for flag: " + tok);
      }
      String value = argv.get(++i);
      a.put(key, value);
    }
    return a;
  }

  private static final class Args {
    private final java.util.Map<String, String> map = new java.util.HashMap<>();

    void put(String k, String v) {
      map.put(k, v);
    }

    String getString(String k) {
      return map.get(k);
    }

    String getStringOr(String k, String defaultValue) {
      String v = map.get(k);
      return v == null ? defaultValue : v;
    }

    int getIntOr(String k, int defaultValue) {
      String v = map.get(k);
      if (v == null) {
        return defaultValue;
      }
      return Integer.parseInt(v);
    }
  }
}
