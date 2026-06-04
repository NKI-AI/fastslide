// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// TypeScript type definitions for FastSlide WASM bindings.
//
// The C++ wrapper exposes a Result-style API: every fallible operation
// returns either `{ok: true, value: T}` or `{ok: false, error: string}`,
// mirroring `aifocore::Result<T>` on the C++ side. No method throws.

/**
 * Result-style return value used throughout the FastSlide WASM API.
 * Mirrors `aifocore::Result<T>` from the C++ codebase.
 */
export type FsResult<T> =
  | { readonly ok: true; readonly value: T }
  | { readonly ok: false; readonly error: string };

/**
 * Level information for a pyramid level.
 */
export interface FastSlideLevelInfo {
  /** Level width in pixels. */
  readonly width: number;
  /** Level height in pixels. */
  readonly height: number;
  /** Downsample factor relative to level 0 (e.g. 1.0, 2.0, 4.0, ...). */
  readonly downsampleFactor: number;
}

/**
 * Decoded region payload.
 *
 * The buffer is always returned in interleaved (HWC) uint8 layout; the
 * wrapper rejects spectral / non-uint8 slides up front, so the data can be
 * blitted directly to a 2D canvas.
 */
export interface FastSlideImage {
  /** Image width in pixels. */
  readonly width: number;
  /** Image height in pixels. */
  readonly height: number;
  /** Number of color channels (1, 3, or 4 for the brightfield viewer). */
  readonly channels: number;
  /** Raw image data in interleaved (HWC) layout. */
  readonly data: Uint8Array;
}

/**
 * FastSlide reader for whole-slide images.
 *
 * Instances are obtained from `FastSlideReader.fromFilePath(...)`. Methods
 * that can fail at runtime (`dimensions`, `mpp`, `getLevelInfo`,
 * `getLevelDimensions`, `readRegion`) return a {@link FsResult}.
 */
export interface FastSlideReader {
  /** Number of pyramid levels in the image. */
  readonly numLevels: number;
  /** Format name (e.g. "Aperio SVS", "MRXS", "QPTIFF"). */
  readonly format: string;

  /** Base dimensions `[width, height]` at level 0. */
  dimensions(): FsResult<[number, number]>;

  /** Microns per pixel as `[mpp_x, mpp_y]` (zeros when unavailable). */
  mpp(): FsResult<[number, number]>;

  /** Information about a specific pyramid level. */
  getLevelInfo(level: number): FsResult<FastSlideLevelInfo>;

  /** Level dimensions `[width, height]` for the given level. */
  getLevelDimensions(level: number): FsResult<[number, number]>;

  /**
   * Read a region from the slide.
   *
   * @param x X coordinate (top-left) in level-native coordinates.
   * @param y Y coordinate (top-left) in level-native coordinates.
   * @param width Region width in pixels.
   * @param height Region height in pixels.
   * @param level Pyramid level (0 = highest resolution).
   */
  readRegion(
    x: number,
    y: number,
    width: number,
    height: number,
    level: number
  ): FsResult<FastSlideImage>;
}

/**
 * FastSlide WASM module interface.
 */
export interface FastSlideModule {
  /** FastSlideReader class with static factory methods. */
  FastSlideReader: {
    /**
     * Create a reader from a virtual filesystem path (typically WORKERFS).
     *
     * @param path Virtual file path (e.g. `/work/slide.svs`).
     */
    fromFilePath(path: string): FsResult<FastSlideReader>;
  };

  /** Emscripten filesystem API. */
  FS: EmscriptenFileSystem;

  /** WORKERFS for lazy file loading. */
  WORKERFS: unknown;
}

/**
 * Emscripten filesystem API (simplified).
 */
export interface EmscriptenFileSystem {
  mkdir(path: string): void;
  mount(type: unknown, opts: unknown, mountpoint: string): void;
  unmount(mountpoint: string): void;
  analyzePath(path: string): { exists: boolean };
  readFile(path: string, opts?: { encoding?: string }): Uint8Array | string;
  writeFile(
    path: string,
    data: string | Uint8Array,
    opts?: { encoding?: string }
  ): void;
  unlink(path: string): void;
}

/**
 * Create and initialize the FastSlide WASM module.
 */
export default function createFastSlideModule(): Promise<FastSlideModule>;
