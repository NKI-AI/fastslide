# AIFO Core Library

A standalone C++ library providing foundational components for the AI for Oncology project with minimal dependencies.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License](https://img.shields.io/badge/License-BSD-blue.svg)](LICENSE)

## Overview

`aifocore` is a modern C++20 library that provides essential utilities, data structures, and algorithms for oncology-focused AI applications. Designed with reusability in mind, it maintains minimal external dependencies and follows best practices for performance, type safety, and flexibility.

## Project Structure

The project is organized into the following directories:

- `include/` - Public header files
- `src/` - Implementation source files
- `tests/` - Unit and integration tests
- `BUILD.bazel` - Bazel build configuration

## Features

### Concepts Module

Type constraints and utilities leveraging C++20 concepts:

Key components:

- `GenericNumber`: Concept for numeric types with arithmetic operations
- `Numeric`: Concept for arithmetic types
- `Size<T, N>`: Multidimensional size/coordinate template class

### Shared Memory Module

Utilities for inter-process communication and data sharing:

- `SharedVector`: Class for managing shared memory vectors
- Thread-safe reference counting
- Efficient data chunking with configurable chunk sizes
- Python bindings for seamless integration with Python applications

### Ranges Module

Utilities for working with ranges and containers:

- `zip`: Implementation for working with multiple containers simultaneously

### Tiling Module

Grid and tiling operations for image processing:

- `Grid<T>`: Class for handling grid operations
- Multiple tiling modes (skip, overflow)
- Different grid traversal orders (C-order, F-order)

### Utilities Module

General-purpose utilities:

- `fmt`: Type-safe formatting utilities
- `vips`: VIPS image processing library wrappers
- `spinners`: Progress indicators for long-running operations
- `temporary`: Temporary file and directory management
- `zip`: Compression and decompression utilities

## Installation

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 19.28+)
- Bazel build system (using bazelisk)
- External dependencies:
  - fmt library
  - cereal library

## Design Philosophy

1. **Modern C++ Approach**: Leveraging C++20 features for cleaner, safer code
2. **Performance-Oriented**: Optimized implementations for compute-intensive tasks
3. **Type Safety**: Strong typing with concept constraints
4. **Minimal Dependencies**: Focused on essential external libraries
5. **Reusability**: Designed to be used across different components

## Contributing

Contributions are welcome! Please follow the Google C++ Style Guide for all submissions.
