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

import (
	"errors"
	"fmt"
)

// Error codes for programmatic error handling
const (
	CodeUnknown          = "unknown"
	CodeNotFound         = "not_found"
	CodeInvalidArgument  = "invalid_argument"
	CodeInternal         = "internal"
	CodeInvalidState     = "invalid_state"
	CodeSlideClosed      = "slide_closed"
	CodeMemoryAllocation = "memory_allocation"
	CodeNotSupported     = "not_supported"
	CodeNotImplemented   = "not_implemented"
)

// Pre-defined errors for common cases
var (
	ErrSlideClosed             = errors.New("slide is closed")
	ErrInvalidDimensions       = errors.New("invalid dimensions: width and height must be greater than 0")
	ErrNoResolutionMetadata    = errors.New("no resolution metadata available")
	ErrNoMagnificationMetadata = errors.New("no magnification metadata available")
)

// FastSlideError represents an error from the FastSlide library.
type FastSlideError struct {
	Op      string // Operation that failed
	Code    string // Error code for programmatic handling
	Message string // Human-readable message
	Err     error  // Underlying error
}

func (e *FastSlideError) Error() string {
	if e.Err != nil {
		return fmt.Sprintf("fastslide %s: %s (%s)", e.Op, e.Message, e.Err.Error())
	}
	return fmt.Sprintf("fastslide %s: %s", e.Op, e.Message)
}

func (e *FastSlideError) Unwrap() error {
	return e.Err
}

// Error constructor functions

func NewOpenError(filename, reason string) error {
	return &FastSlideError{
		Op:      "Open",
		Code:    CodeNotFound,
		Message: fmt.Sprintf("failed to open slide '%s': %s", filename, reason),
	}
}

func NewLevelCountError(reason string) error {
	return &FastSlideError{
		Op:      "GetLevelCount",
		Code:    CodeInternal,
		Message: fmt.Sprintf("failed to get level count: %s", reason),
	}
}

func NewLevelInfoError(level int, reason string) error {
	return &FastSlideError{
		Op:      "GetLevelInfo",
		Code:    CodeInvalidArgument,
		Message: fmt.Sprintf("failed to get level info for level %d: %s", level, reason),
	}
}

func NewLevelDimensionsError(level int, reason string) error {
	return &FastSlideError{
		Op:      "GetLevelDimensions",
		Code:    CodeInvalidArgument,
		Message: fmt.Sprintf("failed to get dimensions for level %d: %s", level, reason),
	}
}

func NewBaseDimensionsError(reason string) error {
	return &FastSlideError{
		Op:      "GetBaseDimensions",
		Code:    CodeInternal,
		Message: fmt.Sprintf("failed to get base dimensions: %s", reason),
	}
}

func NewLevelDownsampleError(level int, reason string) error {
	return &FastSlideError{
		Op:      "GetLevelDownsample",
		Code:    CodeInvalidArgument,
		Message: fmt.Sprintf("failed to get downsample for level %d: %s", level, reason),
	}
}

func NewBestLevelError(downsample float64, reason string) error {
	return &FastSlideError{
		Op:      "GetBestLevelForDownsample",
		Code:    CodeInvalidArgument,
		Message: fmt.Sprintf("failed to get best level for downsample %.2f: %s", downsample, reason),
	}
}

func NewReadRegionError(x, y uint32, level int, reason string) error {
	return &FastSlideError{
		Op:      "ReadRegion",
		Code:    CodeInternal,
		Message: fmt.Sprintf("failed to read region at (%d,%d) level %d: %s", x, y, level, reason),
	}
}

func NewAssociatedImageNamesError(reason string) error {
	return &FastSlideError{
		Op:      "GetAssociatedImageNames",
		Code:    CodeInternal,
		Message: fmt.Sprintf("failed to get associated image names: %s", reason),
	}
}

func NewAssociatedImageDimensionsError(name, reason string) error {
	return &FastSlideError{
		Op:      "GetAssociatedImageDimensions",
		Code:    CodeInvalidArgument,
		Message: fmt.Sprintf("failed to get dimensions for associated image '%s': %s", name, reason),
	}
}

func NewReadAssociatedImageError(name, reason string) error {
	return &FastSlideError{
		Op:      "ReadAssociatedImage",
		Code:    CodeInternal,
		Message: fmt.Sprintf("failed to read associated image '%s': %s", name, reason),
	}
}

func NewMemoryAllocationError(context string) error {
	return &FastSlideError{
		Op:      "MemoryAllocation",
		Code:    CodeMemoryAllocation,
		Message: fmt.Sprintf("failed to allocate memory for %s", context),
	}
}

func WrapUnknownError(operation string) error {
	return &FastSlideError{
		Op:      operation,
		Code:    CodeUnknown,
		Message: "unknown error occurred",
	}
}

// Error checking functions

func IsClosedError(err error) bool {
	var fsErr *FastSlideError
	if errors.As(err, &fsErr) {
		return fsErr.Code == CodeSlideClosed
	}
	return errors.Is(err, ErrSlideClosed)
}

func IsNotFoundError(err error) bool {
	var fsErr *FastSlideError
	if errors.As(err, &fsErr) {
		return fsErr.Code == CodeNotFound
	}
	return false
}

func IsInvalidArgumentError(err error) bool {
	var fsErr *FastSlideError
	if errors.As(err, &fsErr) {
		return fsErr.Code == CodeInvalidArgument
	}
	return false
}

func GetErrorCode(err error) string {
	var fsErr *FastSlideError
	if errors.As(err, &fsErr) {
		return fsErr.Code
	}
	return CodeUnknown
}
