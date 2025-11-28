// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// TypeScript type definitions for FastSlide WASM bindings

/**
 * Level information for a pyramid level
 */
export interface FastSlideLevelInfo {
  /** Level width in pixels */
  readonly width: number;
  /** Level height in pixels */
  readonly height: number;
  /** Downsample factor relative to level 0 (e.g., 1.0, 2.0, 4.0, ...) */
  readonly downsampleFactor: number;
}

/**
 * Image data with resampling capabilities
 */
export interface FastSlideImage {
  /** Image width in pixels */
  readonly width: number;
  /** Image height in pixels */
  readonly height: number;
  /** Number of color channels (typically 3 for RGB) */
  readonly channels: number;
  /** Raw image data as typed array (Uint8Array, Uint16Array, etc.) */
  readonly data: Uint8Array | Uint16Array | Int16Array | Uint32Array | Int32Array | Float32Array | Float64Array;
  
  /**
   * Resample image by power-of-2 factor using box filter averaging
   * @param factor Power-of-2 factor (2, 4, 8, 16, ...)
   * @returns New resampled image
   * @throws Error if factor is not a power of 2
   */
  averageResample(factor: number): FastSlideImage;
}

/**
 * FastSlide reader for whole-slide images
 */
export interface FastSlideReader {
  /** Number of pyramid levels in the image */
  readonly numLevels: number;
  /** Base dimensions [width, height] at level 0 */
  readonly dimensions: number[];
  /** Format name (e.g., "Aperio SVS", "MRXS", "QPTIFF") */
  readonly format: string;
  
  /**
   * Get information about a specific pyramid level
   * @param level Level index (0 = highest resolution)
   * @returns Level information
   */
  getLevelInfo(level: number): FastSlideLevelInfo;
  
  /**
   * Get dimensions for a specific pyramid level
   * @param level Level index (0 = highest resolution)
   * @returns [width, height] tuple
   */
  getLevelDimensions(level: number): number[];
  
  /**
   * Read a region from the slide
   * @param x X coordinate (top-left corner) in level-native coordinates
   * @param y Y coordinate (top-left corner) in level-native coordinates
   * @param width Region width
   * @param height Region height
   * @param level Pyramid level (0 = highest resolution)
   * @returns Image data
   */
  readRegion(x: number, y: number, width: number, height: number, level: number): FastSlideImage;
}

/**
 * FastSlide WASM module interface
 */
export interface FastSlideModule {
  /** FastSlideReader class with static factory methods */
  FastSlideReader: {
    /**
     * Create reader from file path (WORKERFS virtual filesystem)
     * @param path Virtual file path (e.g., "/work/slide.svs")
     * @returns FastSlide reader instance
     * @throws Error if file cannot be opened or is not a supported format
     */
    fromFilePath(path: string): FastSlideReader;
  };
  
  /** FastSlideImage class */
  FastSlideImage: typeof FastSlideImage;
  
  /** FastSlideLevelInfo class */
  FastSlideLevelInfo: typeof FastSlideLevelInfo;
  
  /** Emscripten filesystem API */
  FS: EmscriptenFileSystem;
  
  /** WORKERFS for lazy file loading */
  WORKERFS: any;
}

/**
 * Emscripten filesystem API (simplified)
 */
export interface EmscriptenFileSystem {
  /**
   * Create directory
   * @param path Directory path
   */
  mkdir(path: string): void;
  
  /**
   * Mount a filesystem
   * @param type Filesystem type (e.g., WORKERFS)
   * @param opts Filesystem options
   * @param mountpoint Mount point path
   */
  mount(type: any, opts: any, mountpoint: string): void;
  
  /**
   * Unmount a filesystem
   * @param mountpoint Mount point path
   */
  unmount(mountpoint: string): void;
  
  /**
   * Check if file exists
   * @param path File path
   * @returns true if file exists
   */
  analyzePath(path: string): { exists: boolean };
  
  /**
   * Read file contents
   * @param path File path
   * @param opts Options
   * @returns File contents
   */
  readFile(path: string, opts?: { encoding?: string }): Uint8Array | string;
  
  /**
   * Write file contents
   * @param path File path
   * @param data Data to write
   * @param opts Options
   */
  writeFile(path: string, data: string | Uint8Array, opts?: { encoding?: string }): void;
  
  /**
   * Delete file
   * @param path File path
   */
  unlink(path: string): void;
}

/**
 * Create and initialize the FastSlide WASM module
 * @returns Promise that resolves to the initialized module
 */
export default function createFastSlideModule(): Promise<FastSlideModule>;

