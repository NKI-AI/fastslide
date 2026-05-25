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
	"image"
	"runtime"
	"unsafe"
)

// ToGoImage converts a fastslide.Image to Go's image.Image.
// This is useful when you need to use Go's image processing libraries.
func (img *Image) ToGoImage() (image.Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "ToGoImage",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	// Get image info
	info, err := img.GetInfo()
	if err != nil {
		return nil, fmt.Errorf("failed to get image info: %w", err)
	}

	// Convert to RGB format if not already
	var rgbImage *Image
	if info.Format == FormatRGB {
		rgbImage = img
	} else {
		rgbImage, err = img.ToRGB()
		if err != nil {
			return nil, fmt.Errorf("failed to convert to RGB: %w", err)
		}
		defer rgbImage.Close()
	}

	// Get RGB data
	rgbData, err := rgbImage.GetRGBData()
	if err != nil {
		return nil, fmt.Errorf("failed to get RGB data: %w", err)
	}

	// Create Go image from RGB data
	goImg := image.NewRGBA(image.Rect(0, 0, int(info.Width), int(info.Height)))

	// Convert RGB to RGBA (add alpha channel)
	for i := 0; i < int(info.Width*info.Height); i++ {
		srcIdx := i * 3
		dstIdx := i * 4

		if srcIdx+2 < len(rgbData) {
			goImg.Pix[dstIdx] = rgbData[srcIdx]     // R
			goImg.Pix[dstIdx+1] = rgbData[srcIdx+1] // G
			goImg.Pix[dstIdx+2] = rgbData[srcIdx+2] // B
			goImg.Pix[dstIdx+3] = 255               // A (fully opaque)
		}
	}

	return goImg, nil
}

// FromGoImage creates a fastslide.Image from a Go image.Image.
// This converts standard Go images to fastslide format using contiguous (interleaved) memory layout.
func FromGoImage(goImg image.Image) (*Image, error) {
	if goImg == nil {
		return nil, &FastSlideError{
			Op:      "FromGoImage",
			Code:    CodeInvalidArgument,
			Message: "input image is nil",
		}
	}

	bounds := goImg.Bounds()
	width := uint32(bounds.Dx())
	height := uint32(bounds.Dy())

	if width == 0 || height == 0 {
		return nil, &FastSlideError{
			Op:      "FromGoImage",
			Code:    CodeInvalidArgument,
			Message: "image has zero dimensions",
		}
	}

	// Create dimensions struct
	dimensions := C.FastSlideImageDimensions{
		width:  C.uint32_t(width),
		height: C.uint32_t(height),
	}

	// Determine the image format and create appropriate fastslide image
	var handle *C.FastSlideImage

	// Check the concrete type of the Go image to determine the best format
	switch img := goImg.(type) {
	case *image.RGBA:
		// Create RGBA fastslide image
		handle = C.fastslide_image_create_rgba(dimensions, C.FASTSLIDE_DATA_TYPE_UINT8)
		if handle == nil {
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to create RGBA fastslide image",
			}
		}

		// Copy pixel data directly since both are RGBA with uint8
		dataPtr := C.fastslide_image_get_data_mutable(handle)
		if dataPtr == nil {
			C.fastslide_image_free(handle)
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to get mutable data pointer",
			}
		}

		// Copy RGBA data (both are contiguous/interleaved format)
		dataSize := width * height * 4 // 4 bytes per RGBA pixel
		C.memcpy(unsafe.Pointer(dataPtr), unsafe.Pointer(&img.Pix[0]), C.size_t(dataSize))

	case *image.NRGBA:
		// Create RGBA fastslide image and convert from non-premultiplied
		handle = C.fastslide_image_create_rgba(dimensions, C.FASTSLIDE_DATA_TYPE_UINT8)
		if handle == nil {
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to create RGBA fastslide image",
			}
		}

		dataPtr := C.fastslide_image_get_data_mutable(handle)
		if dataPtr == nil {
			C.fastslide_image_free(handle)
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to get mutable data pointer",
			}
		}

		// Convert NRGBA to RGBA by copying directly (NRGBA is non-premultiplied RGBA)
		dataSize := width * height * 4
		C.memcpy(unsafe.Pointer(dataPtr), unsafe.Pointer(&img.Pix[0]), C.size_t(dataSize))

	case *image.Gray:
		// Create grayscale fastslide image
		handle = C.fastslide_image_create_grayscale(dimensions, C.FASTSLIDE_DATA_TYPE_UINT8)
		if handle == nil {
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to create grayscale fastslide image",
			}
		}

		dataPtr := C.fastslide_image_get_data_mutable(handle)
		if dataPtr == nil {
			C.fastslide_image_free(handle)
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to get mutable data pointer",
			}
		}

		// Copy grayscale data
		dataSize := width * height // 1 byte per pixel
		C.memcpy(unsafe.Pointer(dataPtr), unsafe.Pointer(&img.Pix[0]), C.size_t(dataSize))

	default:
		// For other image types, convert to RGBA manually
		handle = C.fastslide_image_create_rgba(dimensions, C.FASTSLIDE_DATA_TYPE_UINT8)
		if handle == nil {
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to create RGBA fastslide image",
			}
		}

		dataPtr := C.fastslide_image_get_data_mutable(handle)
		if dataPtr == nil {
			C.fastslide_image_free(handle)
			return nil, &FastSlideError{
				Op:      "FromGoImage",
				Code:    CodeInternal,
				Message: "failed to get mutable data pointer",
			}
		}

		// Convert pixel by pixel for generic image types
		pixelData := make([]uint8, width*height*4)
		idx := 0
		for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
			for x := bounds.Min.X; x < bounds.Max.X; x++ {
				r, g, b, a := goImg.At(x, y).RGBA()
				// Convert from uint32 (0-65535) to uint8 (0-255)
				pixelData[idx] = uint8(r >> 8)   // R
				pixelData[idx+1] = uint8(g >> 8) // G
				pixelData[idx+2] = uint8(b >> 8) // B
				pixelData[idx+3] = uint8(a >> 8) // A
				idx += 4
			}
		}

		// Copy the converted pixel data
		C.memcpy(unsafe.Pointer(dataPtr), unsafe.Pointer(&pixelData[0]), C.size_t(len(pixelData)))
	}

	// Create the Go wrapper
	img := &Image{handle: handle}
	runtime.SetFinalizer(img, (*Image).finalize)
	return img, nil
}
