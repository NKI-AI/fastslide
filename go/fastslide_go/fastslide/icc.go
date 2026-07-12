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
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// ColorSpace is the target color space for ICC color management.
type ColorSpace int

const (
	ColorSpaceRGB       ColorSpace = C.FASTSLIDE_COLOR_SPACE_RGB       // Standard RGB
	ColorSpaceLinear    ColorSpace = C.FASTSLIDE_COLOR_SPACE_LINEAR    // Linear-light RGB
	ColorSpaceSRGB      ColorSpace = C.FASTSLIDE_COLOR_SPACE_SRGB      // sRGB (OpenSlide-compatible)
	ColorSpaceAutomatic ColorSpace = C.FASTSLIDE_COLOR_SPACE_AUTOMATIC // Determined automatically
)

// RenderingIntent is the ICC rendering intent for color management.
type RenderingIntent int

const (
	IntentPerceptual           RenderingIntent = C.FASTSLIDE_RENDERING_INTENT_PERCEPTUAL
	IntentRelativeColorimetric RenderingIntent = C.FASTSLIDE_RENDERING_INTENT_RELATIVE_COLORIMETRIC
	IntentSaturation           RenderingIntent = C.FASTSLIDE_RENDERING_INTENT_SATURATION
	IntentAbsoluteColorimetric RenderingIntent = C.FASTSLIDE_RENDERING_INTENT_ABSOLUTE_COLORIMETRIC
)

// OpenOptions controls how a slide is opened. The zero value leaves color
// unmanaged (equivalent to Open). When ApplyICC is true and the slide carries
// an embedded ICC profile, ReadRegion returns pixels already converted to
// TargetColorSpace using RenderingIntent.
type OpenOptions struct {
	ApplyICC         bool
	TargetColorSpace ColorSpace
	RenderingIntent  RenderingIntent
}

// DefaultOpenOptions returns options that leave color unmanaged but default the
// target space to sRGB and intent to perceptual for when ApplyICC is toggled on.
func DefaultOpenOptions() OpenOptions {
	return OpenOptions{
		ApplyICC:         false,
		TargetColorSpace: ColorSpaceSRGB,
		RenderingIntent:  IntentPerceptual,
	}
}

// OpenWithOptions opens a slide file with the given options (e.g. ICC color
// management) and returns a SlideReader handle.
func OpenWithOptions(filename string, opts OpenOptions) (*SlideReader, error) {
	cFilename := C.CString(filename)
	defer C.free(unsafe.Pointer(cFilename))

	var cOpts C.FastSlideOpenOptions
	if opts.ApplyICC {
		cOpts.apply_icc = 1
	}
	cOpts.target_color_space = C.FastSlideColorSpace(opts.TargetColorSpace)
	cOpts.rendering_intent = C.FastSlideRenderingIntent(opts.RenderingIntent)

	C.fastslide_clear_last_error()
	handle := C.fastslide_create_reader_with_options(cFilename, &cOpts)
	if handle == nil {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		if errorMsg == "" {
			errorMsg = "unknown error"
		}
		return nil, NewOpenError(filename, errorMsg)
	}

	reader := &SlideReader{
		handle: handle,
		closed: 0,
	}
	runtime.SetFinalizer(reader, (*SlideReader).finalize)
	return reader, nil
}

// ICCProfile returns the slide's embedded ICC profile bytes, or nil if the
// slide has none. The bytes are returned verbatim regardless of whether ICC
// color management is enabled.
func (sr *SlideReader) ICCProfile() ([]byte, error) {
	if sr.IsClosed() {
		return nil, ErrSlideClosed
	}

	C.fastslide_clear_last_error()
	size := C.fastslide_slide_reader_get_icc_profile_size(sr.handle)
	if size == 0 {
		return nil, nil
	}

	buf := make([]byte, int(size))
	written := C.fastslide_slide_reader_read_icc_profile(
		sr.handle, (*C.uint8_t)(unsafe.Pointer(&buf[0])), C.size_t(size))
	runtime.KeepAlive(sr)
	if written == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return nil, &FastSlideError{
			Op:      "ICCProfile",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	return buf[:int(written)], nil
}

// EnableICCTransform enables in-library ICC color management for subsequent
// ReadRegion calls, converting to target with the given intent. Enabling on a
// slide without an embedded profile is a no-op (reads stay native).
func (sr *SlideReader) EnableICCTransform(target ColorSpace, intent RenderingIntent) error {
	if sr.IsClosed() {
		return ErrSlideClosed
	}

	C.fastslide_clear_last_error()
	rc := C.fastslide_slide_reader_enable_icc_transform(
		sr.handle, C.FastSlideColorSpace(target), C.FastSlideRenderingIntent(intent))
	runtime.KeepAlive(sr)
	if rc == 0 {
		errorMsg := C.GoString(C.fastslide_get_last_error())
		return &FastSlideError{
			Op:      "EnableICCTransform",
			Code:    CodeInternal,
			Message: errorMsg,
		}
	}
	return nil
}
