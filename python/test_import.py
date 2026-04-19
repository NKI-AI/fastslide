#!/usr/bin/env python3
"""Test fastslide import."""

import sys

try:
    import fastslide

    print("✓ Successfully imported fastslide")
    print(f"✓ Available attributes: {', '.join([x for x in dir(fastslide) if not x.startswith('_')])}")

    # Try to check if FastSlide class is available
    if hasattr(fastslide, "FastSlide"):
        print("✓ FastSlide class is available")
    else:
        print("✗ FastSlide class not found")
        sys.exit(1)

except ImportError as e:
    print(f"✗ Failed to import fastslide: {e}")
    import traceback

    traceback.print_exc()
    sys.exit(1)
except Exception as e:
    print(f"✗ Unexpected error: {e}")
    import traceback

    traceback.print_exc()
    sys.exit(1)

print("\n✓ All tests passed!")
