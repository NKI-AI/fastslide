#!/bin/bash
# Build FastSlide wheels for all supported platforms
#
# Strategy:
#   - macOS wheels: Built NATIVELY (requires macOS host with system SDK)
#   - Linux wheels: Built HERMETICALLY (cross-compiled via Zig from any host)
#
# This approach is necessary because some dependencies have macOS-specific code
# that requires Apple frameworks (CoreFoundation/CFTimeZone in Abseil) which are
# not available in hermetic toolchains like Zig SDK.

set -e # Exit on error
set -u # Exit on undefined variable

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Detect host platform
HOST_OS=$(uname -s | tr '[:upper:]' '[:lower:]')
HOST_ARCH=$(uname -m)

# macOS platforms (built natively on macOS)
MACOS_PLATFORMS=(
  "darwin_aarch64"
  "darwin_x86_64"
)

# Linux platforms (built hermetically via cross-compilation)
LINUX_PLATFORMS=(
  "linux_x86_64"
)

# Python versions to build wheels for
PYTHON_VERSIONS=(
  "cp39"  # Python 3.9
  "cp310" # Python 3.10
  "cp311" # Python 3.11
  "cp312" # Python 3.12
  "cp313" # Python 3.13
)

# Output directory
OUTPUT_DIR="bazel-bin/aifo/fastslide/python"

echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}Building FastSlide wheels for all platforms${NC}"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Host: ${HOST_OS} ${HOST_ARCH}${NC}"
echo ""

# Build macOS wheels natively
if [ "$HOST_OS" = "darwin" ]; then
  echo -e "${GREEN}Building macOS wheels (native build)...${NC}"
  for platform in "${MACOS_PLATFORMS[@]}"; do
    for pyver in "${PYTHON_VERSIONS[@]}"; do
      # Extract Python version number (e.g., "cp39" -> "3.9", "cp310" -> "3.10")
      py_major=${pyver:2:1}
      py_minor=${pyver:3} # Get all remaining characters for minor version
      python_version="${py_major}.${py_minor}"

      echo -e "${YELLOW}Building wheel for ${platform} + ${pyver} (Python ${python_version}, native)...${NC}"

      if bazelisk build \
        --@rules_python//python/config_settings:python_version=${python_version} \
        --platforms=//platforms:$platform \
        //aifo/fastslide/python:fastslide_wheel_${pyver}; then
        echo -e "${GREEN}✓ Successfully built wheel for ${platform} + ${pyver}${NC}"
      else
        echo -e "${RED}✗ Failed to build wheel for ${platform} + ${pyver}${NC}"
        exit 1
      fi
    done
    echo ""
  done
else
  echo -e "${YELLOW}⚠ Skipping macOS wheels (requires macOS host)${NC}"
  echo ""
fi

# Build Linux wheels hermetically (can be done from any platform)
echo -e "${GREEN}Building Linux wheels (hermetic cross-compilation)...${NC}"
for platform in "${LINUX_PLATFORMS[@]}"; do
  for pyver in "${PYTHON_VERSIONS[@]}"; do
    # Extract Python version number (e.g., "cp39" -> "3.9", "cp310" -> "3.10")
    py_major=${pyver:2:1}
    py_minor=${pyver:3} # Get all remaining characters for minor version
    python_version="${py_major}.${py_minor}"

    echo -e "${YELLOW}Building wheel for ${platform} + ${pyver} (Python ${python_version}, hermetic)...${NC}"

    if bazelisk build \
      --config=hermetic \
      --@rules_python//python/config_settings:python_version=${python_version} \
      --platforms=//platforms:$platform \
      //aifo/fastslide/python:fastslide_wheel_${pyver}; then
      echo -e "${GREEN}✓ Successfully built wheel for ${platform} + ${pyver}${NC}"
    else
      echo -e "${RED}✗ Failed to build wheel for ${platform} + ${pyver}${NC}"
      exit 1
    fi
  done
  echo ""
done

echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}All wheels built successfully!${NC}"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "Wheels are located in: ${YELLOW}${OUTPUT_DIR}/${NC}"
echo ""
echo "Built wheels:"
ls -lh "${OUTPUT_DIR}"/*.whl 2>/dev/null || echo "No wheels found in ${OUTPUT_DIR}"
echo ""

# Display wheel information
echo "Wheel filenames (for verification):"
for wheel in "${OUTPUT_DIR}"/*.whl; do
  if [ -f "$wheel" ]; then
    basename "$wheel"
  fi
done
