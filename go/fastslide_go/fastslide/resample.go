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
	"runtime"
)

// LanczosResample performs high-quality image resampling using the Lanczos3 kernel.
// The image will be automatically converted to separate planar configuration if needed.
func (img *Image) LanczosResample(outputWidth, outputHeight int) (*Image, error) {
	if outputWidth <= 0 || outputHeight <= 0 {
		return nil, &FastSlideError{
			Op:      "LanczosResample",
			Code:    CodeInvalidArgument,
			Message: "output dimensions must be greater than zero",
		}
	}
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "LanczosResample",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	// Call C function
	C.fastslide_clear_last_error()
	handle := C.fastslide_lanczos_resample(
		img.handle,
		C.uint32_t(outputWidth),
		C.uint32_t(outputHeight),
	)
	runtime.KeepAlive(img)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "LanczosResample",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	resampledImage := &Image{handle: handle}
	runtime.SetFinalizer(resampledImage, (*Image).finalize)

	return resampledImage, nil
}

// Lanczos2Resample performs high-quality image resampling using the Lanczos2 kernel.
// The image will be automatically converted to separate planar configuration if needed.
func (img *Image) Lanczos2Resample(outputWidth, outputHeight uint32) (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "Lanczos2Resample",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	if outputWidth == 0 || outputHeight == 0 {
		return nil, &FastSlideError{
			Op:      "Lanczos2Resample",
			Code:    CodeInvalidArgument,
			Message: "output dimensions must be greater than zero",
		}
	}

	// Call C function
	C.fastslide_clear_last_error()
	handle := C.fastslide_lanczos2_resample(
		img.handle,
		C.uint32_t(outputWidth),
		C.uint32_t(outputHeight),
	)
	runtime.KeepAlive(img)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "Lanczos2Resample",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	resampledImage := &Image{handle: handle}
	runtime.SetFinalizer(resampledImage, (*Image).finalize)

	return resampledImage, nil
}

// CosineResample performs high-quality image resampling using the Cosine-windowed sinc kernel.
// The image will be automatically converted to separate planar configuration if needed.
func (img *Image) CosineResample(outputWidth, outputHeight uint32) (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "CosineResample",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	if outputWidth == 0 || outputHeight == 0 {
		return nil, &FastSlideError{
			Op:      "CosineResample",
			Code:    CodeInvalidArgument,
			Message: "output dimensions must be greater than zero",
		}
	}

	// Call C function
	C.fastslide_clear_last_error()
	handle := C.fastslide_cosine_resample(
		img.handle,
		C.uint32_t(outputWidth),
		C.uint32_t(outputHeight),
	)
	runtime.KeepAlive(img)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "CosineResample",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	resampledImage := &Image{handle: handle}
	runtime.SetFinalizer(resampledImage, (*Image).finalize)

	return resampledImage, nil
}

// AverageResample performs fast downsampling by averaging pixels in blocks.
// The factor must be a power of two and greater than 0.
// The image will be automatically converted to separate planar configuration if needed.
func (img *Image) AverageResample(factor int) (*Image, error) {
	if factor <= 0 {
		return nil, &FastSlideError{
			Op:      "AverageResample",
			Code:    CodeInvalidArgument,
			Message: "factor must be greater than zero",
		}
	}
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "AverageResample",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	if factor == 0 {
		return nil, &FastSlideError{
			Op:      "AverageResample",
			Code:    CodeInvalidArgument,
			Message: "factor must be greater than zero",
		}
	}

	// Check if factor is a power of 2
	if (factor & (factor - 1)) != 0 {
		return nil, &FastSlideError{
			Op:      "AverageResample",
			Code:    CodeInvalidArgument,
			Message: "factor must be a power of two",
		}
	}

	// Call C function
	C.fastslide_clear_last_error()
	handle := C.fastslide_average_resample(
		img.handle,
		C.uint32_t(factor),
	)
	runtime.KeepAlive(img)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "AverageResample",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	resampledImage := &Image{handle: handle}
	runtime.SetFinalizer(resampledImage, (*Image).finalize)

	return resampledImage, nil
}

// Average2x2Resample performs fast 2x2 downsampling by averaging pixels.
// This is a convenience function equivalent to AverageResample(2).
// The image will be automatically converted to separate planar configuration if needed.
func (img *Image) Average2x2Resample() (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "Average2x2Resample",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	// Call C function
	C.fastslide_clear_last_error()
	handle := C.fastslide_average_2x2_resample(img.handle)
	runtime.KeepAlive(img)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "Average2x2Resample",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	resampledImage := &Image{handle: handle}
	runtime.SetFinalizer(resampledImage, (*Image).finalize)

	return resampledImage, nil
}

// Average4x4Resample performs fast 4x4 downsampling by averaging pixels.
// This is a convenience function equivalent to AverageResample(4).
// The image will be automatically converted to separate planar configuration if needed.
func (img *Image) Average4x4Resample() (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "Average4x4Resample",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	// Call C function
	C.fastslide_clear_last_error()
	handle := C.fastslide_average_4x4_resample(img.handle)
	runtime.KeepAlive(img)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "Average4x4Resample",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	resampledImage := &Image{handle: handle}
	runtime.SetFinalizer(resampledImage, (*Image).finalize)

	return resampledImage, nil
}

// Average8x8Resample performs fast 8x8 downsampling by averaging pixels.
// This is a convenience function equivalent to AverageResample(8).
// The image will be automatically converted to separate planar configuration if needed.
func (img *Image) Average8x8Resample() (*Image, error) {
	if img.handle == nil {
		return nil, &FastSlideError{
			Op:      "Average8x8Resample",
			Code:    CodeInvalidState,
			Message: "image is closed",
		}
	}

	// Call C function
	C.fastslide_clear_last_error()
	handle := C.fastslide_average_8x8_resample(img.handle)
	runtime.KeepAlive(img)

	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "Average8x8Resample",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}

	resampledImage := &Image{handle: handle}
	runtime.SetFinalizer(resampledImage, (*Image).finalize)

	return resampledImage, nil
}
