// Copyright 2025 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package fastslide

/*
#include "fastslide/c/fastslide.h"
#include <stdlib.h>
#include <string.h>
*/
import "C"

import (
	"fmt"
	"runtime"
	"sync/atomic"
	"unsafe"
)

// SlideReader wraps a FastSlideSlideReader handle. The underlying FastSlide library is thread-safe.
type SlideReader struct {
	handle *C.FastSlideSlideReader
	closed int32 // Use atomic operations: 0 = open, 1 = closed
}

// RegionSpec represents a region specification.
type RegionSpec struct {
	X      uint32
	Y      uint32
	Width  uint32
	Height uint32
	Level  int
}

// ColorRGB represents an RGB color.
type ColorRGB struct {
	R, G, B uint8
}

// ChannelMetadata contains metadata for a single channel.
type ChannelMetadata struct {
	Name         string
	Biomarker    string
	Color        ColorRGB
	ExposureTime uint32
	SignalUnits  uint32
}

// LevelInfo contains information about a pyramid level.
type LevelInfo struct {
	Width            uint32
	Height           uint32
	DownsampleFactor float64
}

// SlideProperties contains slide properties.
type SlideProperties struct {
	MppX                   float64
	MppY                   float64
	ObjectiveMagnification float64
	ObjectiveName          string
	ScannerModel           string
	ScanDate               string
}

// Bounds represents the bounding box of non-empty slide region.
type Bounds struct {
	X      int64
	Y      int64
	Width  int64
	Height int64
}

// Open opens a slide file and returns a SlideReader handle.
func Open(filename string) (*SlideReader, error) {
	cFilename := C.CString(filename)
	defer C.free(unsafe.Pointer(cFilename))

	C.fastslide_clear_last_error()

	handle := C.fastslide_create_reader(cFilename)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		if errorMsg == "" {
			errorMsg = "unknown error"
		}
		return nil, NewOpenError(filename, errorMsg)
	}

	reader := &SlideReader{
		handle: handle,
		closed: 0, // Initialize as open
	}

	// Set finalizer to ensure cleanup if Close() is not called
	runtime.SetFinalizer(reader, (*SlideReader).finalize)

	return reader, nil
}

// Close closes the slide reader and releases resources.
func (sr *SlideReader) Close() {
	if atomic.CompareAndSwapInt32(&sr.closed, 0, 1) { // Only close if not already closed
		C.fastslide_slide_reader_free(sr.handle)
		sr.handle = nil
		runtime.SetFinalizer(sr, nil)
	}
}

// finalize is called by the garbage collector if Close() wasn't called.
func (sr *SlideReader) finalize() {
	sr.Close()
}

// IsClosed returns true if the slide reader has been closed.
func (sr *SlideReader) IsClosed() bool {
	return atomic.LoadInt32(&sr.closed) == 1
}

// LevelCount returns the number of pyramid levels.
func (sr *SlideReader) LevelCount() (int, error) {
	if sr.IsClosed() {
		return 0, ErrSlideClosed
	}

	count := C.fastslide_slide_reader_get_level_count(sr.handle)
	if count < 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return 0, NewLevelCountError(errorMsg)
	}
	return int(count), nil
}

// LevelInfo returns information about a specific level.
func (sr *SlideReader) LevelInfo(level int) (LevelInfo, error) {
	if sr.IsClosed() {
		return LevelInfo{}, ErrSlideClosed
	}

	var cInfo C.FastSlideLevelInfo
	if C.fastslide_slide_reader_get_level_info(sr.handle, C.int(level), &cInfo) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return LevelInfo{}, NewLevelInfoError(level, errorMsg)
	}

	return LevelInfo{
		Width:            uint32(cInfo.dimensions.width),
		Height:           uint32(cInfo.dimensions.height),
		DownsampleFactor: float64(cInfo.downsample_factor),
	}, nil
}

// LevelDimensions returns the width and height of a specific level.
func (sr *SlideReader) LevelDimensions(level int) (uint32, uint32, error) {
	if sr.IsClosed() {
		return 0, 0, ErrSlideClosed
	}

	var dimensions C.FastSlideImageDimensions
	if C.fastslide_slide_reader_get_level_dimensions(sr.handle, C.int(level), &dimensions) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return 0, 0, NewLevelDimensionsError(level, errorMsg)
	}
	return uint32(dimensions.width), uint32(dimensions.height), nil
}

// BaseDimensions returns the width and height of level 0.
func (sr *SlideReader) BaseDimensions() (uint32, uint32, error) {
	if sr.IsClosed() {
		return 0, 0, ErrSlideClosed
	}

	var dimensions C.FastSlideImageDimensions
	if C.fastslide_slide_reader_get_base_dimensions(sr.handle, &dimensions) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return 0, 0, NewBaseDimensionsError(errorMsg)
	}
	return uint32(dimensions.width), uint32(dimensions.height), nil
}

// LevelDownsample returns the downsample factor for a specific level.
func (sr *SlideReader) LevelDownsample(level int) (float64, error) {
	if sr.IsClosed() {
		return 0, ErrSlideClosed
	}

	downsample := C.fastslide_slide_reader_get_level_downsample(sr.handle, C.int(level))
	if downsample <= 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return 0, NewLevelDownsampleError(level, errorMsg)
	}
	return float64(downsample), nil
}

// BestLevelForDownsample returns the best level for a given downsample factor.
func (sr *SlideReader) BestLevelForDownsample(downsample float64) (int, error) {
	if sr.IsClosed() {
		return 0, ErrSlideClosed
	}

	level := C.fastslide_slide_reader_get_best_level_for_downsample(sr.handle, C.double(downsample))
	if level < 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return 0, NewBestLevelError(downsample, errorMsg)
	}
	return int(level), nil
}

// GetProperties returns slide properties.
func (sr *SlideReader) GetProperties() (SlideProperties, error) {
	if sr.IsClosed() {
		return SlideProperties{}, ErrSlideClosed
	}

	var cProps C.FastSlideSlideProperties
	if C.fastslide_slide_reader_get_properties(sr.handle, &cProps) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return SlideProperties{}, &FastSlideError{
			Op:      "GetProperties",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	defer C.fastslide_slide_reader_free_properties(&cProps)

	props := SlideProperties{
		MppX:                   float64(cProps.mpp_x),
		MppY:                   float64(cProps.mpp_y),
		ObjectiveMagnification: float64(cProps.objective_magnification),
	}

	if cProps.objective_name != nil {
		props.ObjectiveName = C.GoString(cProps.objective_name)
	}
	if cProps.scanner_model != nil {
		props.ScannerModel = C.GoString(cProps.scanner_model)
	}
	if cProps.scan_date != nil {
		props.ScanDate = C.GoString(cProps.scan_date)
	}

	return props, nil
}

// FormatName returns the slide format name.
func (sr *SlideReader) FormatName() (string, error) {
	if sr.IsClosed() {
		return "", ErrSlideClosed
	}

	format := C.fastslide_slide_reader_get_format_name(sr.handle)
	if format == nil {
		return "Unknown", nil
	}
	return C.GoString(format), nil
}

// GetImageFormat returns the image format (RGB or Spectral).
func (sr *SlideReader) GetImageFormat() (ImageFormat, error) {
	if sr.IsClosed() {
		return FormatRGB, ErrSlideClosed
	}

	format := C.fastslide_slide_reader_get_image_format(sr.handle)
	return ImageFormat(format), nil
}

// Bounds returns the bounding box of non-empty slide region.
func (sr *SlideReader) Bounds() (Bounds, error) {
	if sr.IsClosed() {
		return Bounds{}, ErrSlideClosed
	}

	var cBounds C.FastSlideBounds
	if C.fastslide_slide_reader_get_bounds(sr.handle, &cBounds) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return Bounds{}, &FastSlideError{
			Op:      "Bounds",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	return Bounds{
		X:      int64(cBounds.x),
		Y:      int64(cBounds.y),
		Width:  int64(cBounds.width),
		Height: int64(cBounds.height),
	}, nil
}

// ReadRegion reads a region from the slide using coordinates.
func (sr *SlideReader) ReadRegion(x, y, width, height uint32, level int) (*Image, error) {
	if sr.IsClosed() {
		return nil, ErrSlideClosed
	}

	if width == 0 || height == 0 {
		return nil, ErrInvalidDimensions
	}

	C.fastslide_clear_last_error()
	handle := C.fastslide_slide_reader_read_region_coords(
		sr.handle,
		C.uint32_t(x), C.uint32_t(y),
		C.uint32_t(width), C.uint32_t(height),
		C.int(level),
	)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, NewReadRegionError(x, y, level, errorMsg)
	}

	image := &Image{handle: handle}
	runtime.SetFinalizer(image, (*Image).finalize)
	return image, nil
}

// ReadRegionWithSpec reads a region using a RegionSpec.
func (sr *SlideReader) ReadRegionWithSpec(spec RegionSpec) (*Image, error) {
	return sr.ReadRegion(spec.X, spec.Y, spec.Width, spec.Height, spec.Level)
}

// GetChannelMetadata returns metadata for all channels.
func (sr *SlideReader) GetChannelMetadata() ([]ChannelMetadata, error) {
	if sr.IsClosed() {
		return nil, ErrSlideClosed
	}

	var cMetadata *C.FastSlideChannelMetadata
	var count C.int

	C.fastslide_clear_last_error()
	if C.fastslide_slide_reader_get_channel_metadata(sr.handle, &cMetadata, &count) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "GetChannelMetadata",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	defer C.fastslide_slide_reader_free_channel_metadata(cMetadata, count)

	result := make([]ChannelMetadata, int(count))
	if count > 0 {
		slice := unsafe.Slice(cMetadata, int(count))
		for i, meta := range slice {
			result[i] = ChannelMetadata{
				Name:         C.GoString(meta.name),
				Biomarker:    C.GoString(meta.biomarker),
				Color:        ColorRGB{R: uint8(meta.color.r), G: uint8(meta.color.g), B: uint8(meta.color.b)},
				ExposureTime: uint32(meta.exposure_time),
				SignalUnits:  uint32(meta.signal_units),
			}
		}
	}
	return result, nil
}

// AssociatedImageNames returns the names of available associated images.
func (sr *SlideReader) AssociatedImageNames() ([]string, error) {
	if sr.IsClosed() {
		return nil, ErrSlideClosed
	}

	var names **C.char
	var count C.int

	if C.fastslide_slide_reader_get_associated_image_names(sr.handle, &names, &count) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, NewAssociatedImageNamesError(errorMsg)
	}
	defer C.fastslide_slide_reader_free_associated_image_names(names, count)

	result := make([]string, int(count))
	if count > 0 {
		slice := unsafe.Slice(names, int(count))
		for i, name := range slice {
			result[i] = C.GoString(name)
		}
	}
	return result, nil
}

// AssociatedImageDimensions returns the dimensions of a specific associated image.
func (sr *SlideReader) AssociatedImageDimensions(name string) (uint32, uint32, error) {
	if sr.IsClosed() {
		return 0, 0, ErrSlideClosed
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	var dimensions C.FastSlideImageDimensions
	if C.fastslide_slide_reader_get_associated_image_dimensions(sr.handle, cName, &dimensions) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return 0, 0, NewAssociatedImageDimensionsError(name, errorMsg)
	}
	return uint32(dimensions.width), uint32(dimensions.height), nil
}

// ReadAssociatedImage reads an associated image.
func (sr *SlideReader) ReadAssociatedImage(name string) (*Image, error) {
	if sr.IsClosed() {
		return nil, ErrSlideClosed
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	C.fastslide_clear_last_error()
	handle := C.fastslide_slide_reader_read_associated_image(sr.handle, cName)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, NewReadAssociatedImageError(name, errorMsg)
	}

	image := &Image{handle: handle}
	runtime.SetFinalizer(image, (*Image).finalize)
	return image, nil
}

// LevelInfoString returns a summary string for a level.
func (sr *SlideReader) LevelInfoString(level int) (string, error) {
	width, height, err := sr.LevelDimensions(level)
	if err != nil {
		return "", err
	}
	downsample, err := sr.LevelDownsample(level)
	if err != nil {
		return "", err
	}
	return fmt.Sprintf("Level %d: %d×%d (downsample: %.2fx)", level, width, height, downsample), nil
}
