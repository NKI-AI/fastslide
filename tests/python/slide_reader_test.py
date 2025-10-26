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
"""
Comprehensive tests for FastSlide SlideImage Python API

Tests cover:
- Factory methods and object creation
- Properties and metadata access
- Region reading with level-native coordinates
- Coordinate conversion utilities
- Associated images with lazy loading
- Cache management integration
- Context manager support
- Error handling and edge cases
- Thread safety
"""

import pytest
import numpy as np
import threading
import time
from pathlib import Path
from unittest.mock import patch, MagicMock
from typing import Any, Generator

import fastslide


class TestFastSlideFactory:
    """Test factory methods for creating FastSlide objects."""

    def test_from_file_path_success(self, sample_slide_path: str) -> None:
        """Test successful slide creation from file path."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        assert slide is not None
        assert not slide.closed
        assert slide.source_path == sample_slide_path
        slide.close()

    def test_from_file_path_nonexistent_file(self) -> None:
        """Test error handling for non-existent files."""
        with pytest.raises(RuntimeError, match="Failed to open slide"):
            fastslide.FastSlide.from_file_path("nonexistent_file.svs")

    def test_from_file_path_unsupported_format(self, tmp_path: Path) -> None:
        """Test error handling for unsupported file formats."""
        unsupported_file = tmp_path / "test.txt"
        unsupported_file.write_text("not a slide file")

        with pytest.raises(RuntimeError, match="Failed to open slide"):
            fastslide.FastSlide.from_file_path(str(unsupported_file))

    def test_from_uri_not_implemented(self) -> None:
        """Test that URI loading is not yet implemented."""
        with pytest.raises(RuntimeError, match="URI-based loading not yet implemented"):
            fastslide.FastSlide.from_uri("gs://bucket/slide.svs")

    def test_from_file_path_with_spaces(self, tmp_path: Path) -> None:
        """Test file path handling with spaces and special characters."""
        # This would need a real slide file with spaces in the name
        # For now, just test that the path is handled correctly
        path_with_spaces = "path with spaces/slide file.svs"
        with pytest.raises(RuntimeError):  # File doesn't exist, but path should be handled
            fastslide.FastSlide.from_file_path(path_with_spaces)

    def test_from_file_path_pathlib_support(self, sample_slide_path: str) -> None:
        """Test that pathlib.Path objects are supported."""
        from pathlib import Path

        path_obj = Path(sample_slide_path)
        slide = fastslide.FastSlide.from_file_path(path_obj)
        assert slide is not None
        assert not slide.closed
        assert slide.source_path == sample_slide_path
        slide.close()


class TestFastSlideProperties:
    """Test FastSlide properties and metadata access."""

    @pytest.fixture
    def slide(self, sample_slide_path: str) -> Generator[fastslide.FastSlide, None, None]:
        """Fixture providing a FastSlide for testing."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        yield slide
        slide.close()

    def test_basic_properties(self, slide: fastslide.FastSlide) -> None:
        """Test basic slide properties."""
        # Dimensions should be a tuple of two integers
        dimensions = slide.dimensions
        assert isinstance(dimensions, tuple)
        assert len(dimensions) == 2
        assert all(isinstance(d, int) and d > 0 for d in dimensions)

        # Level count should be positive integer
        assert isinstance(slide.level_count, int)
        assert slide.level_count > 0

        # Format should be a non-empty string
        assert isinstance(slide.format, str)
        assert len(slide.format) > 0

        # Source path should match what we opened
        assert isinstance(slide.source_path, str)
        assert slide.source_path == sample_slide_path

        # Should not be closed initially
        assert not slide.closed

    def test_level_dimensions(self, slide: fastslide.FastSlide) -> None:
        """Test level dimensions property."""
        level_dimensions = slide.level_dimensions
        assert isinstance(level_dimensions, tuple)
        assert len(level_dimensions) == slide.level_count

        # Each level should have width, height tuple
        for i, (width, height) in enumerate(level_dimensions):
            assert isinstance(width, int) and width > 0
            assert isinstance(height, int) and height > 0

            # Higher levels should generally be smaller (downsampled)
            if i > 0:
                prev_width, prev_height = level_dimensions[i - 1]
                assert width <= prev_width
                assert height <= prev_height

        # Level 0 dimensions should match overall dimensions
        assert level_dimensions[0] == slide.dimensions

    def test_level_downsamples(self, slide: fastslide.FastSlide) -> None:
        """Test level downsample factors."""
        level_downsamples = slide.level_downsamples
        assert isinstance(level_downsamples, tuple)
        assert len(level_downsamples) == slide.level_count

        # Each downsample should be a positive number
        for i, downsample in enumerate(level_downsamples):
            assert isinstance(downsample, (int, float))
            assert downsample > 0

            # Level 0 should have downsample of 1.0
            if i == 0:
                assert downsample == 1.0
            else:
                # Higher levels should have higher downsample factors
                assert downsample >= level_downsamples[i - 1]

    def test_properties_dict(self, slide: fastslide.FastSlide) -> None:
        """Test properties dictionary contains expected metadata."""
        props = slide.properties
        assert isinstance(props, dict)

        # Should contain basic properties
        expected_keys = ["mpp_x", "mpp_y", "source_path"]
        for key in expected_keys:
            assert key in props

        # MPP values should be positive numbers
        assert isinstance(props["mpp_x"], (int, float))
        assert isinstance(props["mpp_y"], (int, float))
        assert props["mpp_x"] > 0
        assert props["mpp_y"] > 0

        # Source path should match
        assert props["source_path"] == slide.source_path

    def test_best_level_for_downsample(self, slide: fastslide.FastSlide) -> None:
        """Test best level selection for downsample factors."""
        # Test with various downsample factors
        test_downsamples = [1.0, 2.0, 4.0, 8.0, 16.0]

        for downsample in test_downsamples:
            best_level = slide.get_best_level_for_downsample(downsample)
            assert isinstance(best_level, int)
            assert 0 <= best_level < slide.level_count

            # The selected level's downsample should be close to requested
            actual_downsample = slide.level_downsamples[best_level]
            assert actual_downsample <= downsample * 2  # Should be reasonable


class TestFastSlideRegionReading:
    """Test region reading functionality with level-native coordinates."""

    @pytest.fixture
    def slide(self, sample_slide_path: str) -> Generator[fastslide.FastSlide, None, None]:
        """Fixture providing a FastSlide for testing."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        yield slide
        slide.close()

    def test_read_region_basic(self, slide: fastslide.FastSlide) -> None:
        """Test basic region reading functionality."""
        # Read a small region from level 0
        width, height = slide.dimensions
        x = min(width // 4, 1000)
        y = min(height // 4, 1000)
        tile_width, tile_height = 256, 256

        region = slide.read_region((x, y), 0, (tile_width, tile_height))

        # Check return type and shape
        assert isinstance(region, np.ndarray)
        assert region.shape == (tile_height, tile_width, 3)
        assert region.dtype == np.uint8

        # Check that we got actual image data (not all zeros)
        assert not np.all(region == 0)

    def test_read_region_different_levels(self, slide: fastslide.FastSlide) -> None:
        """Test reading regions from different pyramid levels."""
        tile_size = 128

        for level in range(min(3, slide.level_count)):
            level_width, level_height = slide.level_dimensions[level]

            # Calculate safe coordinates within the level
            x = min(level_width // 4, level_width - tile_size)
            y = min(level_height // 4, level_height - tile_size)

            if x >= 0 and y >= 0:  # Make sure coordinates are valid
                region = slide.read_region((x, y), level, (tile_size, tile_size))

                assert region.shape == (tile_size, tile_size, 3)
                assert region.dtype == np.uint8

    def test_read_region_edge_cases(self, slide: fastslide.FastSlide) -> None:
        """Test region reading edge cases."""
        level_width, level_height = slide.level_dimensions[0]

        # Test reading near edges
        edge_size = 64

        # Near right edge
        if level_width > edge_size:
            region = slide.read_region((level_width - edge_size, 0), 0, (edge_size, edge_size))
            assert region.shape == (edge_size, edge_size, 3)

        # Near bottom edge
        if level_height > edge_size:
            region = slide.read_region((0, level_height - edge_size), 0, (edge_size, edge_size))
            assert region.shape == (edge_size, edge_size, 3)

    def test_read_region_invalid_coordinates(self, slide: fastslide.FastSlide) -> None:
        """Test error handling for invalid coordinates."""
        level_width, level_height = slide.level_dimensions[0]

        # Test coordinates outside slide bounds
        with pytest.raises(RuntimeError):
            slide.read_region((level_width + 1000, 0), 0, (256, 256))

        with pytest.raises(RuntimeError):
            slide.read_region((0, level_height + 1000), 0, (256, 256))

    def test_read_region_invalid_level(self, slide: fastslide.FastSlide) -> None:
        """Test error handling for invalid pyramid levels."""
        with pytest.raises(RuntimeError, match="Invalid level"):
            slide.read_region((0, 0), slide.level_count + 1, (256, 256))

        with pytest.raises(RuntimeError, match="Invalid level"):
            slide.read_region((0, 0), -1, (256, 256))

    def test_read_region_closed_slide(self, sample_slide_path: str) -> None:
        """Test error handling when reading from closed slide."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        slide.close()

        with pytest.raises(RuntimeError, match="slide reader is closed"):
            slide.read_region((0, 0), 0, (256, 256))


class TestCoordinateConversion:
    """Test coordinate conversion utilities."""

    @pytest.fixture
    def slide(self, sample_slide_path: str) -> Generator[fastslide.FastSlide, None, None]:
        """Fixture providing a SlideImage for testing."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        yield slide
        slide.close()

    def test_convert_level0_to_level_native(self, slide: fastslide.FastSlide) -> None:
        """Test conversion from level-0 to level-native coordinates."""
        if slide.level_count < 2:
            pytest.skip("Need at least 2 levels for coordinate conversion testing")

        level0_x, level0_y = 1000, 1000
        target_level = 1

        native_x, native_y = slide.convert_level0_to_level_native(level0_x, level0_y, target_level)

        assert isinstance(native_x, int)
        assert isinstance(native_y, int)
        assert native_x >= 0
        assert native_y >= 0

        # Native coordinates should be smaller for higher levels (downsampled)
        downsample = slide.level_downsamples[target_level]
        expected_x = int(level0_x / downsample)
        expected_y = int(level0_y / downsample)

        # Allow for small rounding differences
        assert abs(native_x - expected_x) <= 1
        assert abs(native_y - expected_y) <= 1

    def test_convert_level_native_to_level0(self, slide: fastslide.FastSlide) -> None:
        """Test conversion from level-native to level-0 coordinates."""
        if slide.level_count < 2:
            pytest.skip("Need at least 2 levels for coordinate conversion testing")

        native_x, native_y = 500, 500
        source_level = 1

        level0_x, level0_y = slide.convert_level_native_to_level0(native_x, native_y, source_level)

        assert isinstance(level0_x, int)
        assert isinstance(level0_y, int)
        assert level0_x >= 0
        assert level0_y >= 0

        # Level-0 coordinates should be larger for higher source levels
        downsample = slide.level_downsamples[source_level]
        expected_x = int(native_x * downsample)
        expected_y = int(native_y * downsample)

        assert level0_x == expected_x
        assert level0_y == expected_y

    def test_coordinate_conversion_roundtrip(self, slide: fastslide.FastSlide) -> None:
        """Test that coordinate conversion is reversible."""
        if slide.level_count < 2:
            pytest.skip("Need at least 2 levels for coordinate conversion testing")

        original_x, original_y = 2000, 1500
        test_level = 1

        # Convert level-0 -> level-native -> level-0
        native_x, native_y = slide.convert_level0_to_level_native(original_x, original_y, test_level)
        roundtrip_x, roundtrip_y = slide.convert_level_native_to_level0(native_x, native_y, test_level)

        # Should get back approximately the same coordinates (within rounding)
        downsample = slide.level_downsamples[test_level]
        tolerance = max(1, int(downsample))

        assert abs(roundtrip_x - original_x) <= tolerance
        assert abs(roundtrip_y - original_y) <= tolerance

    def test_coordinate_conversion_level0(self, slide: fastslide.FastSlide) -> None:
        """Test coordinate conversion for level 0 (should be identity)."""
        x, y = 1000, 1000

        # Level 0 conversions should be identity
        native_x, native_y = slide.convert_level0_to_level_native(x, y, 0)
        assert native_x == x
        assert native_y == y

        level0_x, level0_y = slide.convert_level_native_to_level0(x, y, 0)
        assert level0_x == x
        assert level0_y == y

    def test_coordinate_conversion_closed_slide(self, sample_slide_path: str) -> None:
        """Test error handling for coordinate conversion on closed slide."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        slide.close()

        with pytest.raises(RuntimeError, match="slide reader is closed"):
            slide.convert_level0_to_level_native(1000, 1000, 1)

        with pytest.raises(RuntimeError, match="slide reader is closed"):
            slide.convert_level_native_to_level0(500, 500, 1)


class TestAssociatedImages:
    """Test associated images with lazy loading."""

    @pytest.fixture
    def slide(self, sample_slide_path: str) -> Generator[fastslide.FastSlide, None, None]:
        """Fixture providing a SlideImage for testing."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        yield slide
        slide.close()

    def test_associated_images_access(self, slide: fastslide.FastSlide) -> None:
        """Test basic associated images access."""
        assoc_images = slide.associated_images
        assert assoc_images is not None

        # Should be able to get list of keys without loading
        keys = assoc_images.keys()
        assert isinstance(keys, list)

        # Initially no images should be cached
        assert assoc_images.get_cache_size() == 0

    def test_associated_images_existence_check(self, slide: fastslide.FastSlide) -> None:
        """Test checking if associated images exist without loading."""
        assoc_images = slide.associated_images

        # Test __contains__ method
        for name in assoc_images.keys():
            assert name in assoc_images  # Should not load image
            assert assoc_images.get_cache_size() == 0  # Still no images loaded

        # Test non-existent image
        assert "nonexistent_image" not in assoc_images

    def test_associated_images_dimensions(self, slide: fastslide.FastSlide) -> None:
        """Test getting associated image dimensions without loading."""
        assoc_images = slide.associated_images

        for name in assoc_images.keys():
            # Get dimensions should not load the image
            dims = assoc_images.get_dimensions(name)
            assert isinstance(dims, tuple)
            assert len(dims) == 2
            assert all(isinstance(d, int) and d > 0 for d in dims)

            # Should still not be loaded
            assert assoc_images.get_cache_size() == 0

    def test_associated_images_lazy_loading(self, slide: fastslide.FastSlide) -> None:
        """Test lazy loading of associated images."""
        assoc_images = slide.associated_images
        available_names = assoc_images.keys()

        if not available_names:
            pytest.skip("No associated images available for testing")

        test_name = available_names[0]

        # Initially not loaded
        assert assoc_images.get_cache_size() == 0

        # Access the image (should trigger loading)
        image = assoc_images[test_name]

        # Now should be loaded
        assert assoc_images.get_cache_size() == 1
        assert isinstance(image, np.ndarray)
        assert image.dtype == np.uint8
        assert len(image.shape) == 3
        assert image.shape[2] == 3  # RGB channels

    def test_associated_images_caching(self, slide: fastslide.FastSlide) -> None:
        """Test that accessed images are cached."""
        assoc_images = slide.associated_images
        available_names = assoc_images.keys()

        if not available_names:
            pytest.skip("No associated images available for testing")

        test_name = available_names[0]

        # First access
        image1 = assoc_images[test_name]
        assert assoc_images.get_cache_size() == 1

        # Second access should return same cached image
        image2 = assoc_images[test_name]
        assert assoc_images.get_cache_size() == 1
        assert np.array_equal(image1, image2)

    def test_associated_images_cache_clear(self, slide: fastslide.FastSlide) -> None:
        """Test clearing the associated images cache."""
        assoc_images = slide.associated_images
        available_names = assoc_images.keys()

        if not available_names:
            pytest.skip("No associated images available for testing")

        # Load an image
        test_name = available_names[0]
        _ = assoc_images[test_name]
        assert assoc_images.get_cache_size() == 1

        # Clear cache
        assoc_images.clear_cache()
        assert assoc_images.get_cache_size() == 0

    def test_associated_images_nonexistent(self, slide: fastslide.FastSlide) -> None:
        """Test error handling for non-existent associated images."""
        assoc_images = slide.associated_images

        with pytest.raises(KeyError):
            _ = assoc_images["nonexistent_image"]

        with pytest.raises(KeyError):
            assoc_images.get_dimensions("nonexistent_image")

    def test_associated_images_closed_slide(self, sample_slide_path: str) -> None:
        """Test error handling for associated images on closed slide."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        slide.close()

        with pytest.raises(RuntimeError, match="slide reader is closed"):
            _ = slide.associated_images


class TestCacheIntegration:
    """Test cache management integration with SlideImage."""

    @pytest.fixture
    def slide(self, sample_slide_path: str) -> Generator[fastslide.FastSlide, None, None]:
        """Fixture providing a SlideImage for testing."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        yield slide
        slide.close()

    def test_cache_manager_integration(self, slide: fastslide.FastSlide) -> None:
        """Test setting and using a custom cache manager."""
        # Create custom cache manager
        cache_manager = fastslide.CacheManager.create(capacity=100)

        # Set cache manager
        slide.set_cache_manager(cache_manager)
        assert slide.cache_enabled
        assert slide.get_cache_manager() is cache_manager

    def test_global_cache_integration(self, slide: fastslide.FastSlide) -> None:
        """Test using the global cache."""
        # Use global cache
        slide.use_global_cache()
        assert slide.cache_enabled

    def test_cache_disabled_initially(self, slide: fastslide.FastSlide) -> None:
        """Test that cache is initially disabled."""
        # Cache should be disabled by default
        assert not slide.cache_enabled
        assert slide.get_cache_manager() is None

    def test_cache_with_region_reading(self, slide: fastslide.FastSlide) -> None:
        """Test cache functionality with region reading."""
        # Set up cache
        cache_manager = fastslide.CacheManager.create(capacity=10)
        slide.set_cache_manager(cache_manager)

        # Read same region multiple times
        width, height = slide.dimensions
        x = min(width // 4, 1000)
        y = min(height // 4, 1000)

        # First read (cache miss)
        region1 = slide.read_region((x, y), 0, (256, 256))
        stats1 = cache_manager.get_detailed_stats()

        # Second read (cache hit)
        region2 = slide.read_region((x, y), 0, (256, 256))
        stats2 = cache_manager.get_detailed_stats()

        # Should have same result
        assert np.array_equal(region1, region2)

        # Cache stats should show improvement
        assert stats2.hits >= stats1.hits


class TestContextManager:
    """Test context manager functionality."""

    def test_context_manager_basic(self, sample_slide_path: str) -> None:
        """Test basic context manager usage."""
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            assert not slide.closed
            dimensions = slide.dimensions
            assert len(dimensions) == 2

        # Should be closed after context
        assert slide.closed

    def test_context_manager_exception_handling(self, sample_slide_path: str) -> None:
        """Test context manager properly closes on exceptions."""
        slide = None
        try:
            with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
                assert not slide.closed
                raise ValueError("Test exception")
        except ValueError:
            pass

        # Should still be closed after exception
        assert slide is not None
        assert slide.closed

    def test_context_manager_nested(self, sample_slide_path: str) -> None:
        """Test nested context managers."""
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide1:
            with fastslide.FastSlide.from_file_path(sample_slide_path) as slide2:
                assert not slide1.closed
                assert not slide2.closed
                assert slide1 is not slide2

            assert not slide1.closed
            assert slide2.closed

        assert slide1.closed
        assert slide2.closed


class TestThreadSafety:
    """Test thread safety of SlideImage operations."""

    @pytest.fixture
    def slide(self, sample_slide_path: str) -> Generator[fastslide.FastSlide, None, None]:
        """Fixture providing a SlideImage for testing."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        yield slide
        slide.close()

    def test_concurrent_region_reading(self, slide: fastslide.FastSlide) -> None:
        """Test concurrent region reading from multiple threads."""
        width, height = slide.dimensions
        results = []
        errors = []

        def worker(thread_id: int) -> None:
            try:
                # Each thread reads a different region
                x = (thread_id * 200) % (width - 512) if width > 512 else 0
                y = (thread_id * 150) % (height - 512) if height > 512 else 0

                region = slide.read_region((x, y), 0, (256, 256))
                results.append((thread_id, region.shape))
            except Exception as e:
                errors.append((thread_id, str(e)))

        # Start multiple threads
        threads = []
        num_threads = 4

        for i in range(num_threads):
            thread = threading.Thread(target=worker, args=(i,))
            threads.append(thread)
            thread.start()

        # Wait for all threads
        for thread in threads:
            thread.join()

        # Check results
        assert len(errors) == 0, f"Thread errors: {errors}"
        assert len(results) == num_threads

        # All should have read successfully
        for thread_id, shape in results:
            assert shape == (256, 256, 3)

    def test_concurrent_property_access(self, slide: fastslide.FastSlide) -> None:
        """Test concurrent access to slide properties."""
        results = []
        errors = []

        def worker(thread_id: int) -> None:
            try:
                # Access various properties
                dimensions = slide.dimensions
                level_count = slide.level_count
                properties = slide.properties
                format_name = slide.format

                results.append((thread_id, dimensions, level_count, len(properties), format_name))
            except Exception as e:
                errors.append((thread_id, str(e)))

        # Start multiple threads
        threads = []
        num_threads = 8

        for i in range(num_threads):
            thread = threading.Thread(target=worker, args=(i,))
            threads.append(thread)
            thread.start()

        # Wait for all threads
        for thread in threads:
            thread.join()

        # Check results
        assert len(errors) == 0, f"Thread errors: {errors}"
        assert len(results) == num_threads

        # All should have same property values
        first_result = results[0][1:]  # Skip thread_id
        for result in results[1:]:
            assert result[1:] == first_result  # Skip thread_id


class TestErrorHandling:
    """Test error handling and edge cases."""

    def test_closed_slide_operations(self, sample_slide_path: str) -> None:
        """Test that operations fail appropriately on closed slides."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)
        slide.close()

        # All operations should raise RuntimeError
        with pytest.raises(RuntimeError, match="slide reader is closed"):
            _ = slide.dimensions

        with pytest.raises(RuntimeError, match="slide reader is closed"):
            _ = slide.level_count

        with pytest.raises(RuntimeError, match="slide reader is closed"):
            _ = slide.properties

        with pytest.raises(RuntimeError, match="slide reader is closed"):
            slide.read_region((0, 0), 0, (256, 256))

    def test_double_close(self, sample_slide_path: str) -> None:
        """Test that closing a slide multiple times is safe."""
        slide = fastslide.FastSlide.from_file_path(sample_slide_path)

        # First close
        slide.close()
        assert slide.closed

        # Second close should be safe
        slide.close()
        assert slide.closed

    def test_invalid_region_parameters(self, sample_slide_path: str) -> None:
        """Test error handling for invalid region parameters."""
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            # Zero or negative dimensions
            with pytest.raises(RuntimeError):
                slide.read_region((0, 0), 0, (0, 256))

            with pytest.raises(RuntimeError):
                slide.read_region((0, 0), 0, (-256, 256))


# Test fixtures and utilities


@pytest.fixture(scope="session")
def sample_slide_path() -> str:
    """
    Fixture providing path to a sample slide file for testing.

    Uses the LuCa-7color_Scan1.qptiff test file that's available in the repository.
    """
    # In Bazel, data files are available in the runfiles directory
    test_slide = "LuCa-7color_Scan1.qptiff"

    if not Path(test_slide).exists():
        pytest.skip("Sample slide file not available for testing")

    return test_slide


def pytest_configure(config: Any) -> None:
    """Configure pytest markers."""
    config.addinivalue_line("markers", "slow: marks tests as slow (deselect with '-m \"not slow\"')")
    config.addinivalue_line("markers", "integration: marks tests as integration tests")


if __name__ == "__main__":
    pytest.main([__file__])
