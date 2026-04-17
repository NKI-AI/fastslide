#!/usr/bin/env python3
"""Setup script for SimpleTIFF Python bindings.

This script allows installation using pip:
    pip install .
    pip install -e .  # editable mode

Or using meson directly (recommended):
    meson setup builddir -Dbuild_python_bindings=true
    meson install -C builddir
"""

from pathlib import Path

from setuptools import setup

# Read the README
readme_path = Path(__file__).parent / "README.md"
long_description = readme_path.read_text() if readme_path.exists() else ""

setup(
    name="simpletiff",
    version="0.1.0",
    author="SimpleTIFF Authors",
    description="High-performance TIFF reader with Python bindings",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/aifooncology/simpletiff",
    packages=["simpletiff"],
    package_dir={"": "python"},
    package_data={
        "simpletiff": ["py.typed", "_simpletiff.pyi", "*.so", "*.pyd", "*.dylib"],
    },
    install_requires=[
        "numpy>=1.20.0",
    ],
    python_requires=">=3.11",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: Apache Software License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: C++",
        "Topic :: Scientific/Engineering",
        "Topic :: Scientific/Engineering :: Image Processing",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Typing :: Typed",
    ],
    zip_safe=False,
)
