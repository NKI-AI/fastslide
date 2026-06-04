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

import (
	"testing"
)

func TestPlanarConfigConstants(t *testing.T) {
	// Test that planar config constants are defined
	if PlanarContig == 0 {
		t.Error("PlanarContig should not be zero")
	}
	if PlanarSeparate == 0 {
		t.Error("PlanarSeparate should not be zero")
	}
	if PlanarContig == PlanarSeparate {
		t.Error("PlanarContig and PlanarSeparate should be different")
	}
}

func TestImageResampleMethods(t *testing.T) {
	// Create a test RGB image
	dims := ImageDimensions{Width: 100, Height: 100}
	image, err := CreateRGBImage(dims, DataUInt8)
	if err != nil {
		t.Fatalf("Failed to create test image: %v", err)
	}
	defer image.Close()

	// Test that the methods exist and return proper errors for invalid dimensions
	_, err = image.LanczosResample(0, 50)
	if err == nil {
		t.Error("LanczosResample should return error for zero width")
	}

	_, err = image.LanczosResample(50, 0)
	if err == nil {
		t.Error("LanczosResample should return error for zero height")
	}

	_, err = image.Lanczos2Resample(0, 50)
	if err == nil {
		t.Error("Lanczos2Resample should return error for zero width")
	}

	_, err = image.CosineResample(50, 0)
	if err == nil {
		t.Error("CosineResample should return error for zero height")
	}
}

func TestImagePlanarConfigMethod(t *testing.T) {
	// Create a test RGB image
	dims := ImageDimensions{Width: 10, Height: 10}
	image, err := CreateRGBImage(dims, DataUInt8)
	if err != nil {
		t.Fatalf("Failed to create test image: %v", err)
	}
	defer image.Close()

	// Test that PlanarConfig method exists and returns a valid value
	config := image.PlanarConfig()
	if config != PlanarContig && config != PlanarSeparate {
		t.Errorf("PlanarConfig returned unexpected value: %v", config)
	}

	// Test that GetInfo includes planar config
	info, err := image.GetInfo()
	if err != nil {
		t.Fatalf("Failed to get image info: %v", err)
	}

	if info.PlanarConfig != config {
		t.Errorf("GetInfo().PlanarConfig (%v) does not match PlanarConfig() (%v)", info.PlanarConfig, config)
	}
}

func TestClosedImageResample(t *testing.T) {
	// Test that methods return proper errors when called on closed images
	dims := ImageDimensions{Width: 10, Height: 10}
	image, err := CreateRGBImage(dims, DataUInt8)
	if err != nil {
		t.Fatalf("Failed to create test image: %v", err)
	}

	// Close the image
	image.Close()

	// Test that methods return appropriate errors
	_, err = image.LanczosResample(5, 5)
	if err == nil {
		t.Error("LanczosResample should return error for closed image")
	}

	_, err = image.Lanczos2Resample(5, 5)
	if err == nil {
		t.Error("Lanczos2Resample should return error for closed image")
	}

	_, err = image.CosineResample(5, 5)
	if err == nil {
		t.Error("CosineResample should return error for closed image")
	}

	config := image.PlanarConfig()
	if config != PlanarContig {
		t.Errorf("PlanarConfig on closed image should return default value PlanarContig, got %v", config)
	}
}
