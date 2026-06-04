# Copyright 2025 Jonas Teuwen. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import pytest
import numpy as np
import fastslide


class TestImage:
    """Test fastslide.Image Python API."""

    @pytest.fixture(scope="session")
    def sample_slide_path(self) -> str:
        """
        Fixture providing path to a sample slide file for testing.
        """
        # Try to find a test file. Assuming one exists or use dummy if not.
        # For now, I'll reuse logic from slide_reader_test.py if possible,
        # but I can't easily import it.
        # I'll check for standard test files.
        import os

        potential_files = [
            "LuCa-7color_Scan1.qptiff",
            "cmmu_3316_2016.mrxs",
            "TCGA-CM-5348-01Z-00-DX1.2ad0b8f6-684a-41a7-b568-d6e936794863.svs",
        ]
        for f in potential_files:
            if os.path.exists(f):
                return f
        pytest.skip("No sample slide file available")

    def test_read_region_returns_image(self, sample_slide_path: str) -> None:
        """Test that read_region returns an Image object."""
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            region = slide.read_region((0, 0), 0, (100, 100))
            assert isinstance(region, fastslide.Image)

            # Check properties
            assert region.width == 100
            assert region.height == 100
            assert region.channels > 0
            assert region.dtype in ["uint8", "uint16", "float32"]
            assert region.format in ["RGB", "Spectral", "Gray"]
            assert region.planar_config in ["Contig", "Separate"]

    def test_image_numpy_view(self, sample_slide_path: str) -> None:
        """Test that .numpy() returns a valid view."""
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            image = slide.read_region((0, 0), 0, (100, 50))

            # Get numpy view
            array = image.numpy()
            assert isinstance(array, np.ndarray)

            # Check shape
            if image.planar_config == "Contig":
                assert array.shape == (50, 100, image.channels)
            else:
                assert array.shape == (image.channels, 50, 100)

            # Check data validity (basic check)
            assert not np.all(array == 0)  # Assuming region is not empty

    def test_image_lifetime(self, sample_slide_path: str) -> None:
        """Test that numpy array remains valid even if we don't hold reference to Image object explicitly."""
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            # Create array and drop image reference
            array = slide.read_region((0, 0), 0, (100, 100)).numpy()

            # Force garbage collection (optional, but good for testing)
            import gc

            gc.collect()

            # Access array
            assert array.shape[0] in [100, slide.channel_metadata.__len__()]  # Depending on layout
            # Accessing data shouldn't crash
            _ = array[0, 0]
