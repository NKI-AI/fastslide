// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// Custom OpenLayers Source for FastSlide WASM

/**
 * Custom OpenLayers Source that fetches tiles from FastSlide WASM worker
 */
export class FastSlideSource extends ol.source.TileImage {
  /**
   * Create FastSlide source
   * @param {Object} options Options
   * @param {Worker} options.worker - Web worker handling WASM
   * @param {Array} options.levelInfo - Level information from FastSlide
   * @param {number} options.tileSize - Tile size (default: 256)
   */
  constructor(options) {
    const { worker, levelInfo, tileSize = 256 } = options;

    if (!levelInfo || levelInfo.length === 0) {
      throw new Error("Level info is required");
    }

    // Base dimensions (Level 0)
    const level0 = levelInfo[0];
    const width = level0.width;
    const height = level0.height;

    // Calculate resolutions
    const maxLevel = levelInfo.length - 1;
    const resolutions = new Array(levelInfo.length);

    // Map OL zoom level z to FastSlide level (maxLevel - z)
    for (let i = 0; i < levelInfo.length; i++) {
      // OL Level i corresponds to FastSlide level (maxLevel - i)
      const fsLevel = maxLevel - i;
      resolutions[i] = levelInfo[fsLevel].downsampleFactor;
    }

    // Define extent
    const extent = [0, 0, width, height];

    // Create TileGrid
    const tileGrid = new ol.tilegrid.TileGrid({
      extent: extent,
      origin: [0, height], // Top-Left of the image in map units
      resolutions: resolutions,
      tileSize: [tileSize, tileSize],
    });

    super({
      tileGrid: tileGrid,
      projection: new ol.proj.Projection({
        code: "fastslide-image",
        units: "pixels",
        extent: extent,
      }),
      wrapX: false,
      // Initialize with no-op loader to prevent default loader from setting src
      tileLoadFunction: () => {},
    });

    this.worker = worker;
    this.levelInfo = levelInfo;
    this._tileSize = tileSize;
    this.maxLevel = maxLevel;
    this.tileCache = new Map(); // Cache blob URLs

    // Setup worker handler
    this.pendingRequests = new Map(); // requestKey -> [tile, success, failure]
    this.setupWorkerHandler();

    // Custom tile loading
    // We use different method names to avoid shadowing by instance properties set by OpenLayers parent classes
    this.setTileUrlFunction(this.customTileUrlFunction.bind(this));
    this.setTileLoadFunction(this.customTileLoadFunction.bind(this));
  }

  /**
   * Generate unique URL/ID for tile
   */
  customTileUrlFunction(tileCoord) {
    const z = tileCoord[0];
    const x = tileCoord[1];
    const y = tileCoord[2];
    // Format: "fastslide:{fsLevel}:{x}:{y}"
    // OL z -> FastSlide Level (maxLevel - z)
    const fsLevel = this.maxLevel - z;

    // Use a valid Data URI (1x1 transparent GIF) as a placeholder + ID in hash.
    // This prevents browser errors if the URL is fetched.
    // 1x1 transparent GIF
    const base =
      "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";
    return `${base}#${fsLevel}/${x}/${y}`;
  }

  /**
   * Load tile data
   */
  customTileLoadFunction(tile, url) {
    // If cache has it, set and return IMMEDIATELY.
    if (this.tileCache.has(url)) {
      tile.getImage().src = this.tileCache.get(url);
      return;
    }

    // Parse URL: data:...#level/x/y
    const hashIndex = url.indexOf("#");
    if (hashIndex === -1) {
      console.warn(`[TileSource] Invalid URL format (no hash): ${url}`);
      return;
    }

    const hash = url.substring(hashIndex + 1);
    const parts = hash.split("/"); // ["level", "x", "y"]

    if (parts.length < 3) {
      console.warn(`[TileSource] Invalid hash format: ${hash}`);
      return;
    }

    const fsLevel = parseInt(parts[0], 10);
    const x = parseInt(parts[1], 10);
    const y = parseInt(parts[2], 10);

    // Calculate pixel coordinates for worker
    const levelData = this.levelInfo[fsLevel];
    const tileW = this._tileSize;
    const tileH = this._tileSize;

    const px = x * tileW;
    const py = y * tileH;

    // Clamp to level dimensions
    const width = Math.min(tileW, levelData.width - px);
    const height = Math.min(tileH, levelData.height - py);

    // Register pending request
    if (!this.pendingRequests.has(url)) {
      this.pendingRequests.set(url, []);

      // Send to worker
      this.worker.postMessage({
        type: "readTile",
        data: {
          level: fsLevel,
          x: px,
          y: py,
          width: width,
          height: height,
          tileId: url,
        },
      });
    }

    this.pendingRequests.get(url).push(tile);
  }

  setupWorkerHandler() {
    this.worker.addEventListener("message", (e) => {
      const { type, data } = e.data;

      if (type === "tileData") {
        this.handleTileData(data);
        return;
      }

      if (
        type === "error" &&
        e.data.requestType === "readTile" &&
        e.data.requestData &&
        typeof e.data.requestData.tileId === "string"
      ) {
        this.handleTileError(e.data.requestData.tileId, e.data.message);
      }
    });
  }

  /**
   * Mark pending tiles for a given tileId as failed so OpenLayers stops
   * spinning on them. The original error message (already logged by the
   * worker) is surfaced to the console for debuggability.
   */
  handleTileError(tileId, message) {
    const tiles = this.pendingRequests.get(tileId);
    if (!tiles) {
      return;
    }
    this.pendingRequests.delete(tileId);
    console.warn(`[TileSource] Worker failed for ${tileId}: ${message}`);
    for (const tile of tiles) {
      // OpenLayers TileState.ERROR = 3; setting the state aborts the in-flight
      // load so the viewer doesn't keep retrying the same broken tile.
      tile.setState(3);
    }
  }

  handleTileData(data) {
    const { tileId, width, height, channels, data: imageData } = data;
    const tiles = this.pendingRequests.get(tileId);
    if (!tiles) {
      console.warn(`[TileSource] No pending requests for ${tileId}`);
      return;
    }

    this.pendingRequests.delete(tileId);

    try {
      // Create canvas
      const canvas = document.createElement("canvas");
      canvas.width = width;
      canvas.height = height;
      const ctx = canvas.getContext("2d");
      const canvasImageData = ctx.createImageData(width, height);

      // Fill data (Handle RGB/RGBA/Grayscale)
      if (channels === 3) {
        for (let i = 0; i < width * height; i++) {
          const srcIdx = i * 3;
          const dstIdx = i * 4;
          canvasImageData.data[dstIdx] = imageData[srcIdx];
          canvasImageData.data[dstIdx + 1] = imageData[srcIdx + 1];
          canvasImageData.data[dstIdx + 2] = imageData[srcIdx + 2];
          canvasImageData.data[dstIdx + 3] = 255;
        }
      } else if (channels === 4) {
        canvasImageData.data.set(imageData);
      } else if (channels === 1) {
        for (let i = 0; i < width * height; i++) {
          const val = imageData[i];
          const dstIdx = i * 4;
          canvasImageData.data[dstIdx] = val;
          canvasImageData.data[dstIdx + 1] = val;
          canvasImageData.data[dstIdx + 2] = val;
          canvasImageData.data[dstIdx + 3] = 255;
        }
      }

      ctx.putImageData(canvasImageData, 0, 0);

      canvas.toBlob((blob) => {
        const objectUrl = URL.createObjectURL(blob);
        this.tileCache.set(tileId, objectUrl);

        for (const tile of tiles) {
          const image = tile.getImage();
          // Important: OpenLayers ImageTile needs to know loading finished
          // We set src, which triggers 'load' event on image if it was an Image object.
          // But for ol.TileImage, we need to handle state?
          // Actually, setting src on the image element is enough for OL to pick it up
          // IF we didn't break the flow.

          image.onload = () => {
            // Optional: cleanup if needed, but we cache.
            // URL.revokeObjectURL(objectUrl);
          };
          image.src = objectUrl;
        }
      });
    } catch (err) {
      console.error("Error processing tile", err);
      for (const tile of tiles) {
        tile.setState(3); // ERROR
      }
    }
  }

  destroy() {
    // Cleanup object URLs
    for (const url of this.tileCache.values()) {
      URL.revokeObjectURL(url);
    }
    this.tileCache.clear();
  }
}
