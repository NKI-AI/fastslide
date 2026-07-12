#!/bin/sh
# Refresh the dynamic linker cache so libfastslide.so is discoverable
# immediately after installation.
set -e

if command -v ldconfig >/dev/null 2>&1; then
    ldconfig
fi

exit 0
