Developer Guides
================

Step-by-step guides for extending and integrating FastSlide.

.. toctree::
   :maxdepth: 2
   :caption: Implementation Guides:

   new_reader
   custom_cache
   performance_tuning
   testing
   packages_and_releases

Quick Navigation
----------------

🔧 **Extension Guides**
   - :doc:`new_reader` - Add support for a new slide format
   - :doc:`custom_cache` - Implement custom caching strategies

⚡ **Optimization Guides**  
   - :doc:`performance_tuning` - Optimize for your use case
   - :doc:`testing` - Test your extensions thoroughly

🏗️ **Architecture Guides**
   - :doc:`../architecture` - Understand the system design
   - :doc:`../api/cross_reference` - Navigate between C++ and Python APIs

Getting Started
---------------

Before You Begin
~~~~~~~~~~~~~~~~

**Prerequisites:**
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- Bazel 9+ for building
- Python 3.10+ for Python bindings
- Basic understanding of digital pathology formats

**Development Environment:**
- Git repository cloned locally
- Bazel installed and configured
- IDE with C++20 support (recommended: VSCode, CLion)

**Testing Environment:**
- Sample slide files in different formats
- Unit testing framework (GoogleTest for C++, pytest for Python)

Common Development Workflows
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**1. Adding Format Support**

.. mermaid::

   graph LR
       A[Analyze Format] --> B[Create Reader Class]
       B --> C[Implement Core Methods]  
       C --> D[Add Metadata Parsing]
       D --> E[Write Tests]
       E --> F[Register Format]
       F --> G[Update Documentation]

**2. Performance Optimization**

.. mermaid::

   graph LR
       A[Profile Performance] --> B[Identify Bottlenecks]
       B --> C[Optimize Critical Paths]
       C --> D[Benchmark Changes]
       D --> E[Validate Correctness]
       E --> F[Update Tests]

**3. API Extension**

.. mermaid::

   graph LR
       A[Design API] --> B[Implement C++ Interface]
       B --> C[Add Python Bindings]
       C --> D[Write Documentation]
       D --> E[Create Examples]  
       E --> F[Add Integration Tests]

Development Best Practices
---------------------------

Code Organization
~~~~~~~~~~~~~~~~~

FastSlide follows a modular architecture:

.. code-block:: text

   fastslide/
   ├── include/fastslide/           # Public C++ headers
   │   ├── core/                    # Core data structures
   │   ├── readers/                 # Reader interfaces
   │   ├── formats/                 # Format-specific APIs
   │   ├── runtime/                 # Runtime system
   │   ├── utilities/               # Utility classes
   │   ├── python/                  # Python binding headers
   │   └── c/                       # C interface
   ├── src/                         # Implementation files
   │   ├── readers/                 # Reader implementations
   │   ├── formats/                 # Format parsers
   │   ├── runtime/                 # Runtime system
   │   ├── utilities/               # Utility implementations
   │   ├── python/                  # Python bindings
   │   └── c/                       # C interface impl
   └── tests/                       # Test files

Coding Standards
~~~~~~~~~~~~~~~~

**C++ Guidelines:**
- Follow Google C++ Style Guide
- Use ``aifocore::Result`` for error handling
- Prefer ``std::unique_ptr`` for ownership
- Use ``std::string_view`` for string parameters
- Always use RAII for resource management

**Python Guidelines:**
- Follow PEP 8 style guidelines  
- Use type hints for all public APIs
- Prefer NumPy docstring format
- Include usage examples in docstrings

**Documentation:**
- Doxygen comments for all C++ public APIs
- NumPy docstrings for all Python functions
- Include usage examples
- Document thread safety guarantees

Testing Strategy
~~~~~~~~~~~~~~~~

**Unit Tests:**
- GoogleTest (C++) and pytest (Python)
- Test each component in isolation
- Mock dependencies for focused testing
- Property-based testing for edge cases

**Integration Tests:**
- End-to-end workflows with real slide files
- Multi-threading stress tests  
- Memory leak detection
- Performance regression tests

**Platform Testing:**
- Continuous integration on macOS and Linux (currently internally in our monorepo)
- Multiple compiler versions
- Debug and release builds
- Static analysis integration

Contributing Guidelines
-----------------------

Pull Request Process
~~~~~~~~~~~~~~~~~~~~

1. **Fork** the repository and create a feature branch
2. **Implement** your changes following the coding standards
3. **Test** thoroughly with both unit and integration tests  
4. **Document** new APIs and update existing documentation
5. **Benchmark** performance-critical changes
6. **Submit** pull request with clear description

Code Review Checklist
~~~~~~~~~~~~~~~~~~~~~~

- [ ] **Functionality**: Does it work as intended?
- [ ] **Performance**: No significant regressions?
- [ ] **Memory Safety**: No leaks or undefined behavior? 
- [ ] **Thread Safety**: Safe for concurrent access?
- [ ] **API Design**: Consistent with existing patterns?
- [ ] **Documentation**: Complete and accurate?
- [ ] **Tests**: Adequate coverage and quality?
- [ ] **Platform Support**: Works on all target platforms?

Common Pitfalls
---------------

**Memory Management**
   ❌ Don't use raw ``new``/``delete``
   ✅ Use ``std::make_unique`` and smart pointers

**Error Handling**  
   ❌ Don't throw exceptions in C++ code
   ✅ Use ``aifocore::Result`` consistently

**Thread Safety**
   ❌ Don't assume single-threaded access
   ✅ Design for concurrent access from the start

**Performance**
   ❌ Don't optimize prematurely  
   ✅ Profile first, then optimize critical paths

**API Design**
   ❌ Don't break backward compatibility
   ✅ Add new overloads or optional parameters

Getting Help
------------

**Documentation:**
- :doc:`../api/index` - Complete API reference
- :doc:`../architecture` - System architecture details  
- :doc:`../examples/index` - Working code examples

**Community:**
- GitHub Issues: Bug reports and feature requests
- Pull Requests: Code contributions and improvements
- Discussions: Architecture and design questions

**Internal Resources:**
- Code reviews: Expert feedback on implementations
- Architecture discussions: Design decision rationale
- Performance analysis: Benchmarking and optimization guidance
