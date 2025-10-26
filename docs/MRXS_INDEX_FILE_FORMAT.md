# MRXS Index File Format and Tile Collection

## Overview

This document describes how the MRXS (3DHISTECH/MIRAX) index file is parsed to collect tile metadata. The implementation is based on:

- **OpenSlide Format Documentation**: https://openslide.org/formats/mirax/
- **Benjamin Gilbert's Technical Explanation** (2012-07-24): https://lists.andrew.cmu.edu/pipermail/openslide-users/2012-July/000373.html

## Index File Binary Structure

All integers in the index file are **little-endian 32-bit signed integers** (`int32_t`), and they are **unaligned** (can appear at any byte offset).

### File Header Layout

```
Offset  | Size (bytes) | Field                    | Description
--------|--------------|--------------------------|------------------------------------------
0       | 5            | version                  | ASCII string, always "01.02"
5       | N            | slide_id                 | Variable-length UUID from Slidedat.ini
5+N     | 4            | hierarchical_root_ptr    | Pointer to hierarchical root structure
9+N     | 4            | nonhierarchical_root_ptr | Pointer to non-hierarchical root structure
```

**Note**: The `slide_id` length must be known from `Slidedat.ini` (`GENERAL.SLIDE_ID`) before parsing the index file, as there is no length delimiter in the binary format.

### Implementation: Header Parsing

**Location**: `MrxsReader::ReadLevelTiles()` lines 1003-1028

```cpp
// 1. Read and validate version (5 bytes)
char version[6] = {0};
fread(version, 1, 5, file);
// Must equal "01.02"

// 2. Skip UUID (variable length from INI file)
const size_t uuid_length = slide_info_.slide_id.length();
std::vector<char> uuid_buffer(uuid_length);
fread(uuid_buffer.data(), 1, uuid_length, file);

// 3. Read hierarchical root pointer
int32_t hierarchical_root_pointer = ReadLeInt32(file);
```

## Hierarchical Data Structure (Tiled Pyramid Levels)

### Step 1: Navigate to Level Pointer Array

The hierarchical root pointer points to an array of pointers, one for each zoom level:

```
[hierarchical_root_ptr] → int32_t level_pointers[num_zoom_levels]
```

To find a specific level's data:

```
level_pointer_offset = hierarchical_root_ptr + (4 * level_index)
```

**Implementation**: Lines 1032-1044

```cpp
const int64_t level_pointer_offset =
    hierarchical_root_pointer + (4 * level_index);
fseek(file, level_pointer_offset, SEEK_SET);
int32_t zoom_level_data_pointer = ReadLeInt32(file);
```

### Step 2: Parse Zoom Level Header

Each zoom level has a header structure:

```
Offset | Size | Field               | Value/Description
-------|------|---------------------|----------------------------------
0      | 4    | sentinel_zero       | Always 0 (validation marker)
4      | 4    | data_pages_pointer  | Pointer to first page of tile records
```

**Implementation**: Lines 1051-1073

```cpp
fseek(file, zoom_level_data_pointer, SEEK_SET);

// Read and validate sentinel
int32_t sentinel_value = ReadLeInt32(file);
if (sentinel_value != 0) {
    return error("Expected sentinel value 0");
}

// Read pointer to paged data
int32_t data_pages_pointer = ReadLeInt32(file);
```

### Step 3: Parse Linked List of Data Pages

Tile records are stored in a **linked list of pages**. Each page has:

```
Offset        | Size | Field            | Description
--------------|------|------------------|----------------------------------------
0             | 4    | page_length      | Number of tile records in this page
4             | 4    | next_page_ptr    | Pointer to next page (0 if last page)
8             | 16*N | image_records[N] | Array of N image records (16 bytes each)
```

Each **image record** (16 bytes):

```
Offset | Size | Field            | Description
-------|------|------------------|-----------------------------------------------
0      | 4    | image_index      | Linear tile index = y * IMAGENUMBER_X + x
4      | 4    | data_offset      | Byte offset in data file (Dat_*.dat)
8      | 4    | data_length      | Compressed tile size in bytes
12     | 4    | data_file_number | Index into datafile_paths array from INI
```

**Implementation**: Lines 1087-1224

```cpp
while (true) {
    int32_t page_length = ReadLeInt32(file);
    int32_t next_page_pointer = ReadLeInt32(file);

    // Read all records in this page
    for (int i = 0; i < page_length; ++i) {
        int32_t image_index = ReadLeInt32(file);
        int32_t data_offset = ReadLeInt32(file);
        int32_t data_length = ReadLeInt32(file);
        int32_t data_file_number = ReadLeInt32(file);

        // Process tile record...
    }

    // Check if more pages exist
    if (next_page_pointer == 0) {
        break;  // End of linked list
    }

    // Navigate to next page
    fseek(file, next_page_pointer, SEEK_SET);
}
```

## Tile Subdivision Logic

### Overview

**Source**: Benjamin Gilbert's explanation (steps 8-9), OpenSlide format docs

A single stored image may represent multiple logical tiles due to:

1. **Camera image divisions**: Each camera photo is split into `image_divisions^2` sub-images at high resolution
2. **Level concatenation**: Lower resolution levels combine multiple high-res tiles into single images

### Calculation: Converting Image Index to Tile Coordinates

Given an `image_index` from the index file:

```cpp
// Convert linear index to 2D grid
image_grid_x = image_index % IMAGENUMBER_X;
image_grid_y = image_index / IMAGENUMBER_X;
```

**Implementation**: Lines 1161-1162

### Tile Subdivision Algorithm

**⚠️ IMPLEMENTATION EXTENSION**: This subdivision logic is **NOT explicitly documented** in the OpenSlide specification or Benjamin Gilbert's post. We derived this from analyzing OpenSlide source code and testing with real MRXS files.

For each stored image, we create **`subtiles_per_stored_image^2`** logical tile records:

```cpp
const double sub_tile_width = image_width / subtiles_per_stored_image;
const double sub_tile_height = image_height / subtiles_per_stored_image;

for (int sub_y = 0; sub_y < subtiles_per_stored_image; ++sub_y) {
    for (int sub_x = 0; sub_x < subtiles_per_stored_image; ++sub_x) {
        // Calculate tile grid position
        tile_grid_x = image_grid_x + (sub_x * camera_image_divisions);
        tile_grid_y = image_grid_y + (sub_y * camera_image_divisions);

        // Skip if outside slide bounds
        if (tile_grid_x >= IMAGENUMBER_X || tile_grid_y >= IMAGENUMBER_Y) {
            continue;
        }

        // Create tile record
        MiraxTileRecord tile;
        tile.x = tile_grid_x;
        tile.y = tile_grid_y;
        tile.data_file_number = data_file_number;
        tile.offset = data_offset;
        tile.length = data_length;
        tile.subregion_x = sub_tile_width * sub_x;   // Crop offset within image
        tile.subregion_y = sub_tile_height * sub_y;  // Crop offset within image

        tiles.push_back(tile);
    }
}
```

**Implementation**: Lines 1164-1211

### Key Parameters

- **`subtiles_per_stored_image`**: Computed as `max(1, concatenation_factor / image_divisions)`
- **`concatenation_factor`**: `2^(sum of downsample_exponents)` accumulated across levels
- **`camera_image_divisions`**: From `GENERAL.CameraImageImageDivisionsPerSide` in INI

**Example** (CMU-1.mrxs with `image_divisions=4`):

| Level | Concat Factor | Subtiles per Image | Behavior                             |
| ----- | ------------- | ------------------ | ------------------------------------ |
| 0     | 1             | 1                  | Each image = 1 tile (no subdivision) |
| 1     | 2             | 1                  | Each image = 1 tile                  |
| 2     | 4             | 1                  | Each image = 1 tile                  |
| 3     | 8             | 2                  | Each image → 2×2 = 4 tiles           |
| 4     | 16            | 4                  | Each image → 4×4 = 16 tiles          |

## Validation and Safety Checks

**⚠️ IMPLEMENTATION EXTENSION**: These validation checks are **NOT documented** in the format specification but are critical for handling malformed files.

**Implementation**: Lines 1118-1158

### 1. Data Offset Validation

```cpp
if (data_offset < 0) {
    return error("Invalid negative data offset");
}
```

### 2. Data Length Validation

```cpp
if (data_length <= 0) {
    return error("Invalid data length");
}

// Prevent bad_alloc from unreasonably large tiles
constexpr int64_t kMaxTileSize = 100 * 1024 * 1024;  // 100 MB
if (data_length > kMaxTileSize) {
    return error("Data length exceeds maximum allowed size");
}
```

**Rationale**: Typical JPEG tiles are <10MB. A 100MB limit catches corrupt index files before they cause `std::bad_alloc`.

### 3. Overflow Check

```cpp
const int64_t end_offset = static_cast<int64_t>(data_offset) +
                           static_cast<int64_t>(data_length);
if (end_offset < 0 || end_offset > std::numeric_limits<int64_t>::max() - 1024) {
    return error("Data offset + length causes overflow");
}
```

### 4. Bounds Checking During Tile Read

**Implementation**: `MrxsReader::ReadTileData()` lines 2124-2145

Before reading tile data, we validate that the requested range fits within the data file:

```cpp
// Get file size
fseek(file, 0, SEEK_END);
int64_t file_size = ftell(file);

// Validate bounds
int64_t end_offset = tile.offset + tile.length;
if (end_offset > file_size) {
    return error("Tile data extends beyond file size");
}
```

## Non-Hierarchical Data Structure (Associated Images)

### Structure

Non-hierarchical records follow a similar but simpler structure:

```
[nonhier_root_ptr] → int32_t record_pointers[total_nonhier_count]
```

Each record pointer points to:

```
Offset | Size | Field              | Description
-------|------|--------------------|---------------------------------
0      | 4    | sentinel_zero      | Always 0 (validation)
4      | 4    | data_page_pointer  | Pointer to data page
```

Data page format:

```
Offset | Size | Field           | Description
-------|------|-----------------|----------------------------------------
0      | 4    | page_length     | Number of data items (typically 1)
4      | 4    | next_page_ptr   | Next page pointer (often 0)
8      | 4    | reserved_0      | Reserved (typically 0)
12     | 4    | reserved_1      | Reserved (typically 0)
16     | 4    | data_offset     | Byte offset in data file
20     | 4    | data_size       | Size in bytes
24     | 4    | datafile_number | File index
```

**Implementation**: `MrxsReader::ReadNonHierRecord()` lines 1264-1366

**Source**: OpenSlide docs + Benjamin Gilbert's post (step 10a-10b)

## Differences from Specification

### Extensions Implemented

1. **Tile Subdivision Algorithm** (lines 1164-1211)

   - The exact subdivision logic for `subtiles_per_stored_image > 1` is not documented
   - Derived from OpenSlide C source code behavior and empirical testing

2. **Comprehensive Validation** (lines 1118-1158, 2089-2145)

   - Negative offset checks
   - Maximum tile size limits (100MB)
   - Integer overflow detection
   - File bounds validation
   - Not mentioned in format docs but critical for robustness

3. **Subregion Metadata** (lines 1206-1207)

   - `tile.subregion_x` and `tile.subregion_y` fields
   - Store crop offsets for extracting logical tiles from stored images
   - Used during execution stage to extract correct sub-regions

4. **Thread-Safe Parallel Processing**
   - OpenSlide processes tiles sequentially during index parsing
   - Fastslide separates parsing (single-threaded) from execution (multi-threaded)
   - Two-stage pipeline architecture not present in OpenSlide

### Simplifications

None - we implement the full specification as documented.

## Data Flow Summary

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. Parse Index File Header                                      │
│    - Read version ("01.02")                                     │
│    - Skip slide_id (length from INI)                            │
│    - Read hierarchical_root_ptr                                 │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 2. Navigate to Level Data                                       │
│    - Offset = hierarchical_root_ptr + (4 * level_index)        │
│    - Read zoom_level_data_ptr                                   │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 3. Parse Level Header                                           │
│    - Read sentinel_zero (must be 0)                             │
│    - Read data_pages_ptr                                        │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 4. Traverse Linked List of Pages                                │
│    while (next_page_ptr != 0):                                  │
│       - Read page_length                                        │
│       - Read next_page_ptr                                      │
│       - Read image_records[page_length]                         │
│           * image_index                                         │
│           * data_offset                                         │
│           * data_length                                         │
│           * data_file_number                                    │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 5. Subdivide Images into Tiles (IMPLEMENTATION EXTENSION)       │
│    For each image_record:                                       │
│       - Convert image_index → (image_grid_x, image_grid_y)     │
│       - For each subtile (sub_x, sub_y):                        │
│           * Calculate tile_grid_x, tile_grid_y                  │
│           * Calculate subregion_x, subregion_y (crop offsets)   │
│           * Create MiraxTileRecord                              │
│           * Validate (offset, length, bounds)                   │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ 6. Return vector<MiraxTileRecord>                               │
│    - Used to build spatial index (R-tree)                       │
│    - Enables fast region queries                                │
└─────────────────────────────────────────────────────────────────┘
```

## References

1. **OpenSlide Format Documentation**  
   https://openslide.org/formats/mirax/

2. **Benjamin Gilbert's Technical Explanation** (2012-07-24)  
   https://lists.andrew.cmu.edu/pipermail/openslide-users/2012-July/000373.html  
   Subject: "Introduction to MIRAX/MRXS"

3. **OpenSlide Source Code** (C implementation)  
   `openslide-vendor-mirax.c`, functions:

   - `process_hier_data_pages_from_indexfile()` (lines 766-973)
   - Equivalent to our `ReadLevelTiles()` but with integrated grid insertion

4. **Python Reference Implementation** (MIT license)  
   https://github.com/rharkes/miraxreader  
   Provides independent validation of format interpretation
