# Makefile for building FastSlide documentation (Sphinx + Doxygen)
# For deployment to docs.aifo.dev/fastslide

.PHONY: help clean html doxygen all serve install-deps

# Directories
SOURCEDIR     = source
BUILDDIR      = build
HTMLDIR       = $(BUILDDIR)/html
DOXYGENDIR    = $(BUILDDIR)/doxygen
ROOTDIR       = ..

# Commands
SPHINXBUILD   = sphinx-build
SPHINXOPTS    =
DOXYGEN       = doxygen

# Default target
help:
	@echo "FastSlide Documentation Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make html       Build Sphinx HTML documentation"
	@echo "  make doxygen    Build Doxygen C++ API documentation"
	@echo "  make all        Build both Sphinx and Doxygen documentation"
	@echo "  make clean      Remove all build artifacts"
	@echo "  make serve      Start a local HTTP server to preview docs"
	@echo "  make install-deps Install Python dependencies for building docs"
	@echo ""
	@echo "Output structure for docs.aifo.dev/fastslide:"
	@echo "  $(HTMLDIR)/              - Sphinx documentation (main site)"
	@echo "  $(HTMLDIR)/doxygen/      - Doxygen C++ API reference"

# Install Python dependencies
install-deps:
	@echo "Installing documentation dependencies..."
	uv pip install sphinx sphinx_rtd_theme breathe myst-parser sphinx-design \
	            sphinx-copybutton sphinx-inline-tabs sphinxcontrib-mermaid \
	            sphinxext-opengraph sphinx-autodoc-typehints

# Clean all build artifacts
clean:
	@echo "Cleaning build directory..."
	rm -rf $(BUILDDIR)
	@echo "Clean complete."

# Build Doxygen documentation
doxygen:
	@echo "Building Doxygen C++ API documentation..."
	@mkdir -p $(DOXYGENDIR)
	cd $(ROOTDIR) && $(DOXYGEN) Doxyfile
	@echo "Doxygen documentation built in: $(DOXYGENDIR)"

# Build Sphinx documentation
html: doxygen
	@echo "Building Sphinx documentation..."
	@mkdir -p $(HTMLDIR)
	$(SPHINXBUILD) -b html $(SOURCEDIR) $(HTMLDIR) $(SPHINXOPTS)
	@echo ""
	@echo "Copying Doxygen output to HTML build directory..."
	@mkdir -p $(HTMLDIR)/doxygen
	@cp -r $(DOXYGENDIR)/html/* $(HTMLDIR)/doxygen/
	@echo ""
	@echo "Build complete. The HTML pages are in $(HTMLDIR)"
	@echo ""
	@echo "Documentation structure:"
	@echo "  - Sphinx docs: $(HTMLDIR)/index.html"
	@echo "  - Doxygen C++ API: $(HTMLDIR)/doxygen/index.html"

# Build all documentation
all: clean html
	@echo ""
	@echo "==================================================="
	@echo "All documentation built successfully!"
	@echo "==================================================="
	@echo ""
	@echo "To deploy to docs.aifo.dev/fastslide, sync the contents of:"
	@echo "  $(HTMLDIR)/"
	@echo ""
	@echo "This will make available:"
	@echo "  - https://docs.aifo.dev/fastslide/          (Sphinx)"
	@echo "  - https://docs.aifo.dev/fastslide/doxygen/  (Doxygen)"

# Copy Doxygen output to HTML build directory
copy-doxygen:
	@echo "Copying Doxygen output to HTML build directory..."
	@mkdir -p $(HTMLDIR)/doxygen
	@if [ -d "$(DOXYGENDIR)/html" ]; then \
		cp -r $(DOXYGENDIR)/html/* $(HTMLDIR)/doxygen/; \
		echo "Doxygen documentation copied to $(HTMLDIR)/doxygen/"; \
	else \
		echo "Error: Doxygen output not found. Run 'make doxygen' first."; \
		exit 1; \
	fi

# Serve documentation locally for testing
serve:
	@if [ ! -d "$(HTMLDIR)" ]; then \
		echo "Build directory not found. Running 'make all' first..."; \
		$(MAKE) all; \
	fi
	@echo ""
	@echo "Starting local HTTP server..."
	@echo "Documentation will be available at:"
	@echo "  - http://localhost:8000/              (Sphinx)"
	@echo "  - http://localhost:8000/doxygen/      (Doxygen)"
	@echo ""
	@echo "Press Ctrl+C to stop the server"
	@cd $(HTMLDIR) && python3 -m http.server 8000

