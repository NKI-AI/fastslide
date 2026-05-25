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

// Package fastslide provides Go bindings for the FastSlide C library.
package fastslide

/*
#cgo CFLAGS: -I../../../include
#cgo LDFLAGS: -L../../../ -lfastslide
#include "fastslide/c/fastslide.h"
#include <stdlib.h>
#include <string.h>
*/
import "C"

import (
	"fmt"
	"runtime"
	"unsafe"
)

// Image wraps a FastSlideImage handle.
type Image struct {
	handle *C.FastSlideImage
}

// ImageFormat represents the image format.
type ImageFormat int

const (
	FormatGray     ImageFormat = C.FASTSLIDE_IMAGE_FORMAT_GRAY
	FormatRGB      ImageFormat = C.FASTSLIDE_IMAGE_FORMAT_RGB
	FormatRGBA     ImageFormat = C.FASTSLIDE_IMAGE_FORMAT_RGBA
	FormatSpectral ImageFormat = C.FASTSLIDE_IMAGE_FORMAT_SPECTRAL
)

// DataType represents the image data type.
type DataType int

const (
	DataUInt8   DataType = C.FASTSLIDE_DATA_TYPE_UINT8
	DataUInt16  DataType = C.FASTSLIDE_DATA_TYPE_UINT16
	DataInt16   DataType = C.FASTSLIDE_DATA_TYPE_INT16
	DataUInt32  DataType = C.FASTSLIDE_DATA_TYPE_UINT32
	DataInt32   DataType = C.FASTSLIDE_DATA_TYPE_INT32
	DataFloat32 DataType = C.FASTSLIDE_DATA_TYPE_FLOAT32
	DataFloat64 DataType = C.FASTSLIDE_DATA_TYPE_FLOAT64
)

// PlanarConfig represents the planar configuration.
type PlanarConfig int

const (
	PlanarContig   PlanarConfig = C.FASTSLIDE_PLANAR_CONFIG_CONTIG   // Interleaved: RGBRGBRGB...
	PlanarSeparate PlanarConfig = C.FASTSLIDE_PLANAR_CONFIG_SEPARATE // Planar: RRR...GGG...BBB...
)

// ImageInfo contains information about an image.
type ImageInfo struct {
	Format         ImageFormat
	DataType       DataType
	Width          int
	Height         int
	Channels       int
	BytesPerSample int
	DataSize       int
	PlanarConfig   PlanarConfig
}

// ImageDimensions represents image dimensions.
type ImageDimensions struct {
	Width  int
	Height int
}

func init() {
	// Initialize the FastSlide library
	if C.fastslide_initialize() == 0 {
		panic("Failed to initialize FastSlide library")
	}
}

// GetVersion returns the FastSlide library version.
func GetVersion() string {
	return C.GoString(C.fastslide_c_api_get_version())
}

// GetSupportedExtensions returns a list of supported file extensions.
func GetSupportedExtensions() ([]string, error) {
	var extensions **C.char
	var count C.int

	if C.fastslide_get_supported_extensions(&extensions, &count) == 0 {
		return nil, &FastSlideError{
			Op:      "GetSupportedExtensions",
			Code:    CodeInternal,
			Message: C.GoString(C.fastslide_get_last_error()),
		}
	}
	defer C.fastslide_registry_free_extensions(extensions, count)

	result := make([]string, int(count))
	if count > 0 {
		slice := unsafe.Slice(extensions, int(count))
		for i, ext := range slice {
			result[i] = C.GoString(ext)
		}
	}
	return result, nil
}

// Image methods

// Close closes the image and releases resources.
func (img *Image) Close() {
	if img.handle != nil {
		C.fastslide_image_free(img.handle)
		img.handle = nil
		runtime.SetFinalizer(img, nil)
	}
}

// finalize is called by the garbage collector if Close() wasn't called.
func (img *Image) finalize() {
	img.Close()
}

// GetInfo returns information about the image.
func (img *Image) GetInfo() (ImageInfo, error) {
	if img.handle == nil {
		return ImageInfo{}, &FastSlideError{
			Op:      "GetInfo",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	var cInfo C.FastSlideImageInfo
	if C.fastslide_image_get_info(img.handle, &cInfo) == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return ImageInfo{}, &FastSlideError{
			Op:      "GetInfo",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	return ImageInfo{
		Format:         ImageFormat(cInfo.format),
		DataType:       DataType(cInfo.data_type),
		Width:          int(C.uint32_t(cInfo.width)),
		Height:         int(C.uint32_t(cInfo.height)),
		Channels:       int(C.uint32_t(cInfo.channels)),
		BytesPerSample: int(C.uint64_t(cInfo.bytes_per_sample)),
		DataSize:       int(C.uint64_t(cInfo.data_size)),
		PlanarConfig:   PlanarConfig(cInfo.planar_config),
	}, nil
}

// Width returns the image width.
func (img *Image) Width() int {
	if img.handle == nil {
		return 0
	}
	return int(C.fastslide_image_get_width(img.handle))
}

// Height returns the image height.
func (img *Image) Height() int {
	if img.handle == nil {
		return 0
	}
	return int(C.fastslide_image_get_height(img.handle))
}

// Channels returns the number of channels.
func (img *Image) Channels() int {
	if img.handle == nil {
		return 0
	}
	return int(C.fastslide_image_get_channels(img.handle))
}

// Format returns the image format.
func (img *Image) Format() ImageFormat {
	if img.handle == nil {
		return FormatRGB // Default fallback
	}
	return ImageFormat(C.fastslide_image_get_format(img.handle))
}

// PlanarConfig returns the image planar configuration.
func (img *Image) PlanarConfig() PlanarConfig {
	if img.handle == nil {
		return PlanarContig // Default fallback
	}
	return PlanarConfig(C.fastslide_image_get_planar_config(img.handle))
}

// IsInitialized checks if the image has been initialized.
func (img *Image) IsInitialized() bool {
	if img.handle == nil {
		return false
	}
	return C.fastslide_image_is_initialized(img.handle) != 0
}

// IsEmpty checks if the image is empty.
func (img *Image) IsEmpty() bool {
	if img.handle == nil {
		return true
	}
	return C.fastslide_image_is_empty(img.handle) != 0
}

// Paste pastes another image onto this image at specified coordinates.
func (img *Image) Paste(sourceImage *Image, destX, destY, sourceX, sourceY, sourceWidth, sourceHeight int) error {
	if img.handle == nil {
		return &FastSlideError{
			Op:      "Paste",
			Code:    CodeInvalidState,
			Message: "destination image is closed",
		}
	}
	if sourceImage.handle == nil {
		return &FastSlideError{
			Op:      "Paste",
			Code:    CodeInvalidState,
			Message: "source image is closed",
		}
	}

	C.fastslide_clear_last_error()
	completed := C.fastslide_image_paste(img.handle, sourceImage.handle,
		C.uint32_t(destX), C.uint32_t(destY),
		C.uint32_t(sourceX), C.uint32_t(sourceY),
		C.uint32_t(sourceWidth), C.uint32_t(sourceHeight))
	runtime.KeepAlive(img)
	runtime.KeepAlive(sourceImage)
	if completed == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return &FastSlideError{
			Op:      "Paste",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	return nil
}

// PasteSimple pastes the entire source image at the specified destination coordinates.
func (img *Image) PasteSimple(sourceImage *Image, destX, destY int) error {
	if img.handle == nil {
		return &FastSlideError{
			Op:      "PasteSimple",
			Code:    CodeInvalidState,
			Message: "destination image is closed",
		}
	}
	if destX < 0 || destY < 0 {
		return &FastSlideError{
			Op:      "PasteSimple",
			Code:    CodeInvalidArgument,
			Message: "destination coordinates must be non-negative",
		}
	}
	return img.Paste(sourceImage, destX, destY, 0, 0, 0, 0)
}

// Clone creates a deep copy of the image.
func (img *Image) Clone() (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "Clone",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	C.fastslide_clear_last_error()
	handle := C.fastslide_image_clone(img.handle)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "Clone",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	clonedImage := &Image{handle: handle}
	runtime.SetFinalizer(clonedImage, (*Image).finalize)
	return clonedImage, nil
}

// GetDescription returns a human-readable description of the image.
func (img *Image) GetDescription() (string, error) {
	if img.handle == nil {
		return "", &FastSlideError{
			Op:      "GetDescription",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	buffer := make([]byte, 256)
	length := C.fastslide_image_get_description(img.handle, (*C.char)(unsafe.Pointer(&buffer[0])), C.size_t(len(buffer)))
	if length < 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return "", &FastSlideError{
			Op:      "GetDescription",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	// Find the null terminator
	for i, b := range buffer {
		if b == 0 {
			return string(buffer[:i]), nil
		}
	}
	return string(buffer), nil
}

// String implements fmt.Stringer interface for nice printing.
func (img *Image) String() string {
	if img.handle == nil {
		return "Image{closed}"
	}

	info, err := img.GetInfo()
	if err != nil {
		return fmt.Sprintf("Image{error: %v}", err)
	}

	var formatStr string
	switch info.Format {
	case FormatGray:
		formatStr = "Gray"
	case FormatRGB:
		formatStr = "RGB"
	case FormatRGBA:
		formatStr = "RGBA"
	case FormatSpectral:
		formatStr = "Spectral"
	default:
		formatStr = fmt.Sprintf("Format(%d)", int(info.Format))
	}

	var dataTypeStr string
	switch info.DataType {
	case DataUInt8:
		dataTypeStr = "uint8"
	case DataUInt16:
		dataTypeStr = "uint16"
	case DataInt16:
		dataTypeStr = "int16"
	case DataUInt32:
		dataTypeStr = "uint32"
	case DataInt32:
		dataTypeStr = "int32"
	case DataFloat32:
		dataTypeStr = "float32"
	case DataFloat64:
		dataTypeStr = "float64"
	default:
		dataTypeStr = fmt.Sprintf("DataType(%d)", int(info.DataType))
	}

	return fmt.Sprintf("Image{%s %s %dx%dx%d, %d bytes}",
		formatStr,
		dataTypeStr,
		info.Width,
		info.Height,
		info.Channels,
		info.DataSize,
	)
}

// ToRGB converts the image to RGB format.
func (img *Image) ToRGB() (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "ToRGB",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	C.fastslide_clear_last_error()
	handle := C.fastslide_image_to_rgb(img.handle)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "ToRGB",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	runtime.KeepAlive(img)
	rgbImage := &Image{handle: handle}
	runtime.SetFinalizer(rgbImage, (*Image).finalize)
	return rgbImage, nil
}

// ToPlanar converts the image to planar (separate) memory layout.
// In planar layout, channels are stored separately: RRR...GGG...BBB...
func (img *Image) ToPlanar() (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "ToPlanar",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	C.fastslide_clear_last_error()
	handle := C.fastslide_image_to_planar(img.handle)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "ToPlanar",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	runtime.KeepAlive(img)

	planarImage := &Image{handle: handle}
	runtime.SetFinalizer(planarImage, (*Image).finalize)
	return planarImage, nil
}

// ToInterleaved converts the image to interleaved (contiguous) memory layout.
// In interleaved layout, channels are interleaved: RGBRGBRGB...
func (img *Image) ToInterleaved() (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "ToInterleaved",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	C.fastslide_clear_last_error()
	handle := C.fastslide_image_to_interleaved(img.handle)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "ToInterleaved",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	runtime.KeepAlive(img)

	interleavedImage := &Image{handle: handle}
	runtime.SetFinalizer(interleavedImage, (*Image).finalize)
	return interleavedImage, nil
}

// GetRGBData returns the RGB data as a byte slice. The image must be in RGB format.
func (img *Image) GetRGBData() ([]uint8, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "GetRGBData",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	// Get image info to check format and get dimensions
	info, err := img.GetInfo()
	if err != nil {
		return nil, err
	}

	if info.Format != FormatRGB {
		return nil, &FastSlideError{
			Op:      "GetRGBData",
			Code:    CodeInvalidState,
			Message: "image must be in RGB format",
		}
	}

	// Get data pointer
	dataPtr := C.fastslide_image_get_data(img.handle)
	if dataPtr == nil {
		return nil, &FastSlideError{
			Op:      "GetRGBData",
			Code:    CodeInternal,
			Message: "failed to get image data",
		}
	}

	// Convert C data to Go slice
	dataSize := info.Width * info.Height * 3 // RGB = 3 bytes per pixel
	data := C.GoBytes(unsafe.Pointer(dataPtr), C.int(dataSize))

	return data, nil
}

// GetRawData returns the raw image data as a byte slice for any format and data type.
func (img *Image) GetRawData() ([]byte, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "GetRawData",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	// Get image info to determine data size
	info, err := img.GetInfo()
	if err != nil {
		return nil, err
	}

	// Get data pointer
	dataPtr := C.fastslide_image_get_data(img.handle)
	if dataPtr == nil {
		return nil, &FastSlideError{
			Op:      "GetRawData",
			Code:    CodeInternal,
			Message: "failed to get image data",
		}
	}

	// Convert C data to Go slice using the total data size from image info
	data := C.GoBytes(unsafe.Pointer(dataPtr), C.int(info.DataSize))

	return data, nil
}

// CreateBlankImage creates a blank/uninitialized image that adapts to first paste.
func CreateBlankImage(dimensions ImageDimensions) (*Image, error) {
	cDims := C.FastSlideImageDimensions{
		width:  C.uint32_t(dimensions.Width),
		height: C.uint32_t(dimensions.Height),
	}

	C.fastslide_clear_last_error()
	handle := C.fastslide_image_create_blank(cDims)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "CreateBlankImage",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	image := &Image{handle: handle}
	runtime.SetFinalizer(image, (*Image).finalize)
	return image, nil
}
