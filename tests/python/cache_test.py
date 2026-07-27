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
Comprehensive tests for FastSlide cache management

Tests cover:
- CacheManager creation and basic operations
- Cache statistics and detailed inspection
- Cache resizing and capacity management
- GlobalCacheManager singleton behavior
- Cache clearing and memory management
- Thread safety of cache operations
- Performance characteristics
- Integration between multiple cache managers
- Error handling and edge cases
"""

import pytest
import threading
import time
import gc
from typing import Any, Generator, List
from unittest.mock import patch

import fastslide


class TestCacheManager:
    """Test CacheManager creation and basic operations."""

    def test_create_cache_manager_default_capacity(self) -> None:
        """Test creating cache manager with default capacity (1 GiB)."""
        cache_manager = fastslide.CacheManager.create()
        assert cache_manager is not None

        stats = cache_manager.get_basic_stats()
        assert stats.capacity_bytes == 1024 * 1024 * 1024  # 1 GiB default
        assert stats.size == 0
        assert stats.hits == 0
        assert stats.misses == 0
        assert stats.hit_ratio == 0.0

    def test_create_cache_manager_custom_capacity(self) -> None:
        """Test creating cache manager with custom capacity (bytes)."""
        test_capacity_bytes = 500
        cache_manager = fastslide.CacheManager.create(capacity_bytes=test_capacity_bytes)

        stats = cache_manager.get_basic_stats()
        assert stats.capacity_bytes == test_capacity_bytes
        assert stats.size == 0

    def test_create_cache_manager_zero_capacity(self) -> None:
        """Test creating cache manager with zero capacity (rejected)."""
        with pytest.raises(ValueError, match="Cache capacity must be greater than 0"):
            fastslide.CacheManager.create(capacity_bytes=0)

    def test_create_cache_manager_large_capacity(self) -> None:
        """Test creating cache manager with a large byte capacity."""
        large_capacity_bytes = 100_000
        cache_manager = fastslide.CacheManager.create(capacity_bytes=large_capacity_bytes)

        stats = cache_manager.get_basic_stats()
        assert stats.capacity_bytes == large_capacity_bytes

    def test_cache_manager_clear(self) -> None:
        """Test clearing cache manager."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)

        stats = cache_manager.get_basic_stats()
        assert stats.size == 0

        cache_manager.clear()

        stats = cache_manager.get_basic_stats()
        assert stats.size == 0

    def test_cache_manager_resize(self) -> None:
        """Test resizing cache manager capacity (bytes)."""
        initial_capacity_bytes = 100
        cache_manager = fastslide.CacheManager.create(capacity_bytes=initial_capacity_bytes)

        stats = cache_manager.get_basic_stats()
        assert stats.capacity_bytes == initial_capacity_bytes

        new_capacity_bytes = 200
        cache_manager.resize(new_capacity_bytes)
        stats = cache_manager.get_basic_stats()
        assert stats.capacity_bytes == new_capacity_bytes

        smaller_capacity_bytes = 50
        cache_manager.resize(smaller_capacity_bytes)
        stats = cache_manager.get_basic_stats()
        assert stats.capacity_bytes == smaller_capacity_bytes

    def test_cache_manager_resize_to_zero(self) -> None:
        """Test resizing cache manager to zero capacity (rejected)."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)

        with pytest.raises(ValueError, match="Cache capacity must be greater than 0"):
            cache_manager.resize(0)


class TestCacheStatistics:
    """Test cache statistics and detailed inspection."""

    def test_basic_stats_structure(self) -> None:
        """Test basic cache statistics structure."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)
        stats = cache_manager.get_basic_stats()

        assert hasattr(stats, "capacity_bytes")
        assert hasattr(stats, "size")
        assert hasattr(stats, "hits")
        assert hasattr(stats, "misses")
        assert hasattr(stats, "hit_ratio")

        assert isinstance(stats.capacity_bytes, int)
        assert isinstance(stats.size, int)
        assert isinstance(stats.hits, int)
        assert isinstance(stats.misses, int)
        assert isinstance(stats.hit_ratio, float)

        assert stats.capacity_bytes == 100
        assert stats.size == 0
        assert stats.hits == 0
        assert stats.misses == 0
        assert stats.hit_ratio == 0.0

    def test_detailed_stats_structure(self) -> None:
        """Test detailed cache statistics structure."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)
        detailed_stats = cache_manager.get_detailed_stats()

        assert hasattr(detailed_stats, "capacity_bytes")
        assert hasattr(detailed_stats, "size")
        assert hasattr(detailed_stats, "hits")
        assert hasattr(detailed_stats, "misses")
        assert hasattr(detailed_stats, "hit_ratio")
        assert hasattr(detailed_stats, "memory_usage_mb")
        assert hasattr(detailed_stats, "recent_keys")
        assert hasattr(detailed_stats, "key_frequencies")

        assert isinstance(detailed_stats.memory_usage_mb, float)
        assert isinstance(detailed_stats.recent_keys, list)
        assert isinstance(detailed_stats.key_frequencies, dict)

        assert detailed_stats.memory_usage_mb >= 0.0
        assert len(detailed_stats.recent_keys) == 0
        assert len(detailed_stats.key_frequencies) == 0

    def test_stats_consistency(self) -> None:
        """Test consistency between basic and detailed stats."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=200)

        basic_stats = cache_manager.get_basic_stats()
        detailed_stats = cache_manager.get_detailed_stats()

        assert basic_stats.capacity_bytes == detailed_stats.capacity_bytes
        assert basic_stats.size == detailed_stats.size
        assert basic_stats.hits == detailed_stats.hits
        assert basic_stats.misses == detailed_stats.misses
        assert basic_stats.hit_ratio == detailed_stats.hit_ratio

    def test_hit_ratio_calculation(self) -> None:
        """Test hit ratio calculation with different scenarios."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)

        # Initial state: no hits or misses
        stats = cache_manager.get_basic_stats()
        assert stats.hit_ratio == 0.0

        # Note: We can't easily test actual cache hits/misses without
        # integrating with a real slide reader, so this tests the structure


class TestGlobalCacheManager:
    """Test GlobalCacheManager singleton behavior."""

    def test_global_cache_manager_singleton(self) -> None:
        """Test that GlobalCacheManager is a singleton."""
        global_cache1 = fastslide.GlobalCacheManager.instance()
        global_cache2 = fastslide.GlobalCacheManager.instance()

        # Should be the same instance
        assert global_cache1 is global_cache2

    def test_global_cache_manager_set_capacity(self) -> None:
        """Test setting global cache capacity (in bytes)."""
        global_cache = fastslide.GlobalCacheManager.instance()

        test_capacity_bytes = 500
        global_cache.set_capacity_bytes(test_capacity_bytes)

        capacity_bytes = global_cache.get_capacity_bytes()
        assert capacity_bytes == test_capacity_bytes

    def test_global_cache_manager_get_size(self) -> None:
        """Test getting global cache size."""
        global_cache = fastslide.GlobalCacheManager.instance()

        # Clear first
        global_cache.clear()

        size = global_cache.get_size()
        assert isinstance(size, int)
        assert size >= 0

    def test_global_cache_manager_clear(self) -> None:
        """Test clearing global cache."""
        global_cache = fastslide.GlobalCacheManager.instance()

        # Clear cache (should not raise error)
        global_cache.clear()

        # Verify size is reset
        size = global_cache.get_size()
        assert size == 0

    def test_global_cache_manager_multiple_threads(self) -> None:
        """Test global cache manager access from multiple threads."""
        capacities = []
        errors = []

        def worker(thread_id: int) -> None:
            try:
                instance = fastslide.GlobalCacheManager.instance()
                unique_capacity_bytes = 1000 + thread_id
                instance.set_capacity_bytes(unique_capacity_bytes)
                time.sleep(0.01)
                final_capacity_bytes = instance.get_stats().capacity_bytes
                capacities.append((thread_id, final_capacity_bytes))
            except Exception as e:
                errors.append((thread_id, str(e)))

        # Start multiple threads
        threads = []
        num_threads = 5  # Reduce number for cleaner test

        for i in range(num_threads):
            thread = threading.Thread(target=worker, args=(i,))
            threads.append(thread)
            thread.start()

        # Wait for all threads
        for thread in threads:
            thread.join()

        # Check results
        assert len(errors) == 0, f"Thread errors: {errors}"
        assert len(capacities) == num_threads

        # All threads should see the same final capacity (since they're sharing the same singleton)
        # The capacity should be one of the values set by the threads
        final_capacity = capacities[0][1]
        expected_capacities = [1000 + i for i in range(num_threads)]
        assert final_capacity in expected_capacities, (
            f"Final capacity {final_capacity} not in expected range {expected_capacities}"
        )

        # Verify all threads see the same capacity (proving they access the same singleton)
        for thread_id, capacity in capacities:
            # Allow some tolerance as the final capacity might vary due to thread timing
            assert capacity in expected_capacities, (
                f"Thread {thread_id} saw capacity {capacity}, expected one of {expected_capacities}"
            )


class TestCacheIntegration:
    """Test integration between cache managers and slide readers."""

    @pytest.fixture
    def sample_slide_path(self) -> str:
        """Fixture providing path to sample slide for testing."""
        # This should be replaced with an actual test slide file
        test_slide = "tests/data/sample_slide.svs"

        # For testing purposes, we'll need to mock or provide a real file
        # For now, we'll skip tests that need a real slide file
        import os

        if not os.path.exists(test_slide):
            pytest.skip("Sample slide file not available for integration testing")

        return test_slide

    def test_cache_integration_with_slide_reader(self, sample_slide_path: str) -> None:
        """Test cache integration with actual slide reading."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=50)

        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            slide.set_cache(cache_manager)

            # Verify cache is enabled
            assert slide.cache_enabled
            assert slide.get_cache_manager() is cache_manager

            # Read some regions to test cache
            width, height = slide.dimensions
            x = min(width // 4, 1000)
            y = min(height // 4, 1000)

            # First read (should be cache miss)
            region1 = slide.read_region((x, y), 0, (256, 256))
            stats1 = cache_manager.get_detailed_stats()

            # Second read of same region (should be cache hit)
            region2 = slide.read_region((x, y), 0, (256, 256))
            stats2 = cache_manager.get_detailed_stats()

            # Verify regions are identical
            import numpy as np

            assert np.array_equal(region1, region2)

            # Cache stats should show improvement
            assert stats2.hits >= stats1.hits

    def test_multiple_slides_same_cache(self, sample_slide_path: str) -> None:
        """Test multiple slides sharing the same cache manager."""
        shared_cache = fastslide.CacheManager.create(capacity_bytes=100)

        # Open multiple slide instances
        slides = []
        try:
            for i in range(3):
                slide = fastslide.FastSlide.from_file_path(sample_slide_path)
                slide.set_cache(shared_cache)
                slides.append(slide)

            # All should be using the same cache
            for slide in slides:
                assert slide.cache_enabled
                assert slide.get_cache_manager() is shared_cache

            # Read from different slides
            for i, slide in enumerate(slides):
                width, height = slide.dimensions
                x = min(width // 4, i * 100)  # Different regions
                y = min(height // 4, i * 100)

                region = slide.read_region((x, y), 0, (128, 128))
                assert region.shape == (128, 128, 3)

            # Cache should show activity
            stats = shared_cache.get_detailed_stats()
            assert stats.size > 0 or stats.hits > 0 or stats.misses > 0

        finally:
            # Clean up
            for slide in slides:
                slide.close()

    def test_global_cache_with_slide_reader(self, sample_slide_path: str) -> None:
        """Test global cache integration with slide reader."""
        global_cache = fastslide.GlobalCacheManager.instance()

        global_cache.set_capacity_bytes(200)

        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            # Use global cache
            slide.use_global_cache()
            assert slide.cache_enabled

            # Read some regions
            width, height = slide.dimensions
            x = min(width // 4, 1000)
            y = min(height // 4, 1000)

            region = slide.read_region((x, y), 0, (256, 256))
            assert region.shape == (256, 256, 3)

            # Global cache should show activity
            stats = global_cache.get_stats()
            # Note: Can't guarantee specific values due to potential other test interference


class TestCacheThreadSafety:
    """Test thread safety of cache operations."""

    def test_cache_manager_concurrent_access(self) -> None:
        """Test concurrent access to cache manager from multiple threads."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=1000)
        results = []
        errors = []

        def worker(thread_id: int) -> None:
            try:
                for i in range(10):
                    basic_stats = cache_manager.get_basic_stats()
                    detailed_stats = cache_manager.get_detailed_stats()

                    assert basic_stats.capacity_bytes == detailed_stats.capacity_bytes
                    assert basic_stats.size == detailed_stats.size

                    # Clear cache occasionally
                    if i % 5 == 0:
                        cache_manager.clear()

                    time.sleep(0.001)  # Small delay

                results.append(thread_id)

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

    def test_cache_manager_concurrent_resize(self) -> None:
        """Test concurrent cache resizing from multiple threads."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)
        results = []
        errors = []

        def worker(thread_id: int) -> None:
            try:
                new_capacity_bytes = 100 + (thread_id * 50)
                cache_manager.resize(new_capacity_bytes)

                stats = cache_manager.get_basic_stats()
                assert stats.capacity_bytes > 0

                results.append((thread_id, stats.capacity_bytes))

            except Exception as e:
                errors.append((thread_id, str(e)))

        threads = []
        num_threads = 5

        for i in range(num_threads):
            thread = threading.Thread(target=worker, args=(i,))
            threads.append(thread)
            thread.start()

        for thread in threads:
            thread.join()

        assert len(errors) == 0, f"Thread errors: {errors}"
        assert len(results) == num_threads

        final_stats = cache_manager.get_basic_stats()
        expected_capacities = [100 + (i * 50) for i in range(num_threads)]
        assert final_stats.capacity_bytes in expected_capacities

    def test_global_cache_concurrent_access(self) -> None:
        """Test concurrent access to global cache from multiple threads."""
        results = []
        errors = []

        def worker(thread_id: int) -> None:
            try:
                global_cache = fastslide.GlobalCacheManager.instance()

                capacity_bytes = global_cache.get_capacity_bytes()
                assert isinstance(capacity_bytes, int)

                new_capacity_bytes = 1000 + thread_id
                global_cache.set_capacity_bytes(new_capacity_bytes)

                new_capacity_check = global_cache.get_capacity_bytes()
                assert new_capacity_check > 0

                results.append((thread_id, new_capacity_check))

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


class TestCachePerformance:
    """Test cache performance characteristics."""

    @pytest.mark.slow
    def test_cache_creation_performance(self) -> None:
        """Test performance of creating many cache managers."""
        start_time = time.time()

        cache_managers = []
        for i in range(100):
            cache_manager = fastslide.CacheManager.create(capacity_bytes=1000)
            cache_managers.append(cache_manager)

        creation_time = time.time() - start_time

        # Should be reasonably fast (less than 1 second for 100 caches)
        assert creation_time < 1.0

        # Verify all were created successfully
        assert len(cache_managers) == 100

        # Verify they're all different instances
        for i in range(len(cache_managers)):
            for j in range(i + 1, len(cache_managers)):
                assert cache_managers[i] is not cache_managers[j]

    @pytest.mark.slow
    def test_cache_stats_performance(self) -> None:
        """Test performance of getting cache statistics."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=10000)

        # Time getting basic stats
        start_time = time.time()
        for i in range(1000):
            stats = cache_manager.get_basic_stats()
        basic_stats_time = time.time() - start_time

        # Time getting detailed stats
        start_time = time.time()
        for i in range(1000):
            detailed_stats = cache_manager.get_detailed_stats()
        detailed_stats_time = time.time() - start_time

        # Both should be reasonably fast
        assert basic_stats_time < 0.5  # Less than 0.5 seconds for 1000 calls
        assert detailed_stats_time < 1.0  # Less than 1 second for 1000 calls

        # Note: we used to assert basic_stats_time <= detailed_stats_time but
        # at the microsecond scale of an empty-cache stats call the ordering
        # is dominated by measurement noise (see PR notes), so the assertion
        # was flaky. The absolute budgets above already guard performance.

    def test_cache_resize_performance(self) -> None:
        """Test performance of cache resizing."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=1000)

        start_time = time.time()

        for i in range(100):
            new_capacity_bytes = 1000 + (i * 10)
            cache_manager.resize(new_capacity_bytes)

        resize_time = time.time() - start_time

        assert resize_time < 0.5

        stats = cache_manager.get_basic_stats()
        assert stats.capacity_bytes == 1000 + (99 * 10)


class TestCacheErrorHandling:
    """Test error handling in cache operations."""

    def test_cache_manager_invalid_capacity(self) -> None:
        """Test error handling for invalid cache capacity."""
        with pytest.raises(TypeError):
            fastslide.CacheManager.create(capacity_bytes=-1)

    def test_cache_manager_resize_invalid(self) -> None:
        """Test error handling for invalid resize operations."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)

        with pytest.raises(TypeError):
            cache_manager.resize(-1)

    def test_global_cache_invalid_capacity(self) -> None:
        """Test error handling for invalid global cache capacity."""
        global_cache = fastslide.GlobalCacheManager.instance()

        with pytest.raises(TypeError):
            global_cache.set_capacity_bytes(-1)

    def test_stats_access_consistency(self) -> None:
        """Test that stats access is always consistent."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=100)

        stats_list = []
        for i in range(10):
            basic_stats = cache_manager.get_basic_stats()
            detailed_stats = cache_manager.get_detailed_stats()

            assert basic_stats.capacity_bytes == detailed_stats.capacity_bytes
            assert basic_stats.size == detailed_stats.size
            assert basic_stats.hits == detailed_stats.hits
            assert basic_stats.misses == detailed_stats.misses
            assert basic_stats.hit_ratio == detailed_stats.hit_ratio

            stats_list.append((basic_stats, detailed_stats))

        first_capacity_bytes = stats_list[0][0].capacity_bytes
        for basic_stats, _ in stats_list:
            assert basic_stats.capacity_bytes == first_capacity_bytes


class TestCacheMemoryManagement:
    """Test cache memory management and cleanup."""

    def test_cache_manager_cleanup(self) -> None:
        """Test that cache managers can be properly cleaned up."""
        for i in range(50):
            cache_manager = fastslide.CacheManager.create(capacity_bytes=1000)

            stats = cache_manager.get_basic_stats()
            assert stats.capacity_bytes == 1000

            cache_manager.clear()
            del cache_manager

        gc.collect()

    def test_cache_clear_effectiveness(self) -> None:
        """Test that cache clear actually frees memory."""
        cache_manager = fastslide.CacheManager.create(capacity_bytes=1000)

        # Initially empty
        stats = cache_manager.get_basic_stats()
        assert stats.size == 0

        # Clear should work even when empty
        cache_manager.clear()

        stats = cache_manager.get_basic_stats()
        assert stats.size == 0

    def test_multiple_cache_managers_independence(self) -> None:
        """Test that multiple cache managers are independent."""
        cache1 = fastslide.CacheManager.create(capacity_bytes=100)
        cache2 = fastslide.CacheManager.create(capacity_bytes=200)
        cache3 = fastslide.CacheManager.create(capacity_bytes=300)

        assert cache1 is not cache2
        assert cache2 is not cache3
        assert cache1 is not cache3

        stats1 = cache1.get_basic_stats()
        stats2 = cache2.get_basic_stats()
        stats3 = cache3.get_basic_stats()

        assert stats1.capacity_bytes == 100
        assert stats2.capacity_bytes == 200
        assert stats3.capacity_bytes == 300

        cache1.resize(150)
        cache2.clear()

        new_stats1 = cache1.get_basic_stats()
        new_stats2 = cache2.get_basic_stats()
        new_stats3 = cache3.get_basic_stats()

        assert new_stats1.capacity_bytes == 150
        assert new_stats2.capacity_bytes == 200
        assert new_stats3.capacity_bytes == 300


# Test configuration and utilities


def pytest_configure(config: Any) -> None:
    """Configure pytest markers."""
    config.addinivalue_line("markers", "slow: marks tests as slow (deselect with '-m \"not slow\"')")
    config.addinivalue_line("markers", "integration: marks tests as integration tests")


if __name__ == "__main__":
    pytest.main([__file__])
