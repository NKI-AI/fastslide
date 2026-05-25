// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// FastSlide WASM Worker - Handles WASM module loading and tile processing.
//
// The C++ WASM wrapper returns Result-shaped objects of the form
// `{ok: true, value: ...}` or `{ok: false, error: "..."}` for every fallible
// operation. This worker treats anything that isn't `{ok: true}` as a real
// error and surfaces the original C++ error message back to the main thread.

import createFastSlideModule from "./fastslide_multiplex.js";

// Absolute URL of the emscripten loader. The pthread-enabled build spawns
// helper Workers via `new Worker(Module.mainScriptUrlOrBlob, {type: "module"})`
// inside `createFastSlideModule`. If we don't set `mainScriptUrlOrBlob`
// explicitly, emscripten falls back to
// `new URL(EXPORT_NAME, import.meta.url)`, which resolves to
// `<dir>/fastslide_multiplex` (no `.js` suffix) and 404s — the cause of the
// opaque `worker sent an error! undefined:undefined: undefined` messages
// emitted by the loader's `worker.onerror` handler.
const FASTSLIDE_MODULE_URL = new URL(
  "./fastslide_multiplex.js",
  import.meta.url
).href;

let module = null;
let FS = null;
let reader = null;
let tileCache = new Map();

/**
 * Unwrap a Result-shaped value (`{ok, value}` / `{ok, error}`).
 * Throws an Error with the original C++ message on failure.
 * @param {{ok: true, value: any} | {ok: false, error: string}} result
 * @param {string} context Short label included in thrown errors.
 * @returns {any} The unwrapped `value`.
 */
function unwrap(result, context) {
  if (result && result.ok === true) {
    return result.value;
  }
  const message =
    result && typeof result.error === "string" ? result.error : "unknown error";
  throw new Error(`${context}: ${message}`);
}

async function initializeModule() {
  try {
    module = await createFastSlideModule({
      mainScriptUrlOrBlob: FASTSLIDE_MODULE_URL,
    });
    FS = module.FS;

    if (!FS || !module.WORKERFS) {
      throw new Error("FS or WORKERFS not exported from WASM module");
    }

    self.postMessage({
      type: "ready",
      message: "WASM module loaded successfully",
    });
  } catch (err) {
    console.error("Failed to initialize WASM module:", err);
    self.postMessage({
      type: "error",
      message: "Failed to load WASM module: " + err.message,
    });
  }
}

self.onmessage = async function (e) {
  const { type, data } = e.data;

  try {
    switch (type) {
      case "loadFile":
        await handleLoadFile(data);
        break;

      case "getInfo":
        handleGetInfo();
        break;

      case "readTile":
        await handleReadTile(data);
        break;

      case "readRegion":
        await handleReadRegion(data);
        break;

      default:
        console.warn("Unknown message type:", type);
    }
  } catch (err) {
    console.error("Error handling message:", type, err);
    self.postMessage({
      type: "error",
      message: `Error in ${type}: ${err.message}`,
      requestType: type,
      requestData: data,
    });
  }
};

async function handleLoadFile(data) {
  if (!module || !FS) {
    throw new Error("WASM module not initialized");
  }

  // Two payload shapes:
  //   single file:  { file: File }
  //   folder mount: { entries: [{path, file}], entryPath: string }
  // The folder-mount shape is used for container formats (MRXS, DICOM,
  // OME-Zarr) where the reader needs the sibling files visible on disk.
  let entries;
  let entryPath;
  let displayName;
  let displaySize;
  if (data.entries && data.entryPath) {
    entries = data.entries;
    entryPath = data.entryPath;
    const entry = entries.find((e) => e.path === entryPath);
    displayName = entry ? entry.file.name : entryPath;
    displaySize = entries.reduce((sum, e) => sum + (e.file.size || 0), 0);
  } else if (data.file) {
    entries = [{ path: data.file.name, file: data.file }];
    entryPath = data.file.name;
    displayName = data.file.name;
    displaySize = data.file.size;
  } else {
    throw new Error("loadFile: expected either {file} or {entries, entryPath}");
  }

  const startTime = performance.now();

  if (reader) {
    reader = null;
    tileCache.clear();
  }

  try {
    FS.unmount("/work");
  } catch (e) {
    // Not mounted yet; ignore.
  }

  try {
    FS.mkdir("/work");
  } catch (e) {
    // Directory might already exist; ignore.
  }

  // WORKERFS uses `blobs[i].name` verbatim as the in-VFS path under the
  // mount point. By preserving the relative paths from the picked folder
  // we let formats like MRXS find their sibling `<slide>/Slidedat.ini`.
  const blobs = entries.map(({ path, file }) => ({ name: path, data: file }));
  FS.mount(module.WORKERFS, { blobs }, "/work");

  const virtualPath = "/work/" + entryPath;
  const fileInfo = FS.analyzePath(virtualPath);
  if (!fileInfo.exists) {
    throw new Error(`File not found in virtual filesystem: ${virtualPath}`);
  }

  reader = unwrap(
    module.FastSlideReader.fromFilePath(virtualPath),
    "fromFilePath"
  );

  const parseTime = performance.now() - startTime;

  const numLevels = reader.numLevels;
  const dimensions = unwrap(reader.dimensions(), "dimensions");
  const format = reader.format;
  const mpp = unwrap(reader.mpp(), "mpp");

  const levelInfo = [];
  for (let i = 0; i < numLevels; i++) {
    const info = unwrap(reader.getLevelInfo(i), `getLevelInfo(${i})`);
    levelInfo.push({
      level: i,
      width: info.width,
      height: info.height,
      downsampleFactor: info.downsampleFactor,
    });
  }

  self.postMessage({
    type: "slideLoaded",
    data: {
      numLevels,
      dimensions,
      format,
      mpp,
      levelInfo,
      fileName: displayName,
      fileSize: displaySize,
      parseTime,
    },
  });
}

function handleGetInfo() {
  if (!reader) {
    throw new Error("No slide loaded");
  }

  const numLevels = reader.numLevels;
  const dimensions = unwrap(reader.dimensions(), "dimensions");
  const format = reader.format;

  const levelInfo = [];
  for (let i = 0; i < numLevels; i++) {
    const info = unwrap(reader.getLevelInfo(i), `getLevelInfo(${i})`);
    levelInfo.push({
      level: i,
      width: info.width,
      height: info.height,
      downsampleFactor: info.downsampleFactor,
    });
  }

  self.postMessage({
    type: "slideInfo",
    data: {
      numLevels,
      dimensions,
      format,
      levelInfo,
    },
  });
}

async function handleReadTile(data) {
  const { level, x, y, width, height, tileId } = data;

  if (!reader) {
    throw new Error("No slide loaded");
  }

  const cacheKey = `${level}:${x}:${y}:${width}:${height}`;
  if (tileCache.has(cacheKey)) {
    const cached = tileCache.get(cacheKey);
    self.postMessage(
      {
        type: "tileData",
        data: {
          ...cached,
          tileId,
          fromCache: true,
        },
      },
      [cached.data.buffer]
    );
    return;
  }

  const startTime = performance.now();

  const image = unwrap(
    reader.readRegion(x, y, width, height, level),
    `readRegion(level=${level}, ${x},${y} ${width}x${height})`
  );

  const readTime = performance.now() - startTime;

  const imageData = image.data;
  const imageWidth = image.width;
  const imageHeight = image.height;
  const channels = image.channels;

  // The wrapper always returns interleaved (HWC) uint8 data via
  // `Image::MakeInterleaved()`, so we can hand the buffer to the main thread
  // without further conversion.
  const dataArray =
    imageData instanceof Uint8Array
      ? imageData
      : new Uint8Array(imageData.buffer);

  const result = {
    tileId,
    level,
    x,
    y,
    width: imageWidth,
    height: imageHeight,
    channels,
    data: dataArray,
    readTime,
    fromCache: false,
  };

  if (tileCache.size < 100) {
    tileCache.set(cacheKey, {
      level,
      x,
      y,
      width: imageWidth,
      height: imageHeight,
      channels,
      data: new Uint8Array(dataArray),
      readTime,
    });
  }

  self.postMessage(
    {
      type: "tileData",
      data: result,
    },
    [dataArray.buffer]
  );
}

async function handleReadRegion(data) {
  const { level, x, y, width, height } = data;

  if (!reader) {
    throw new Error("No slide loaded");
  }

  const startTime = performance.now();

  const image = unwrap(
    reader.readRegion(x, y, width, height, level),
    `readRegion(level=${level}, ${x},${y} ${width}x${height})`
  );

  const readTime = performance.now() - startTime;

  const imageData = image.data;
  const imageWidth = image.width;
  const imageHeight = image.height;
  const channels = image.channels;

  const dataArray =
    imageData instanceof Uint8Array
      ? imageData
      : new Uint8Array(imageData.buffer);

  self.postMessage(
    {
      type: "regionData",
      data: {
        level,
        x,
        y,
        width: imageWidth,
        height: imageHeight,
        channels,
        data: dataArray,
        readTime,
      },
    },
    [dataArray.buffer]
  );
}

initializeModule();
