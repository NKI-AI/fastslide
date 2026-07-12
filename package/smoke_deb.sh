#!/usr/bin/env bash
# Build the FastSlide Debian packages for one architecture and run the
# standalone Docker smoke test against them.
#
# Usage:
#   package/smoke_deb.sh [amd64|arm64]
#
# Requires: bazelisk (or $BAZEL) and Docker (with the matching --platform
# available, e.g. via Docker Desktop / binfmt on the host). Runs on the macOS
# host too, since Docker Desktop runs Linux containers.
set -euo pipefail

ARCH="${1:-amd64}"
BAZEL="${BAZEL:-bazelisk}"

case "${ARCH}" in
    amd64)
        RUNTIME_TARGET="//package:libfastslide_amd64"
        DOCKER_PLATFORM="linux/amd64"
        ;;
    arm64)
        RUNTIME_TARGET="//package:libfastslide_arm64"
        DOCKER_PLATFORM="linux/arm64"
        ;;
    *)
        echo "Usage: $0 [amd64|arm64]" >&2
        exit 2
        ;;
esac

DEV_TARGET="//package:libfastslide_dev"

WORKSPACE_DIR="$("${BAZEL}" info workspace)"
BAZEL_BIN="$("${BAZEL}" info bazel-bin)"

echo "==> Building ${RUNTIME_TARGET} and ${DEV_TARGET}"
"${BAZEL}" build "${RUNTIME_TARGET}" "${DEV_TARGET}"

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

cp "${BAZEL_BIN}"/package/*.deb "${STAGE}/"
cp "${WORKSPACE_DIR}/package/deb_smoke.cpp" "${STAGE}/"
cp "${WORKSPACE_DIR}/package/Dockerfile" "${STAGE}/"

echo "==> Staged packages:"
ls -l "${STAGE}"/*.deb

echo "==> Running Docker smoke (${DOCKER_PLATFORM})"
docker build --platform "${DOCKER_PLATFORM}" -t "fastslide-deb-smoke:${ARCH}" "${STAGE}"

echo "==> Debian package smoke test passed for ${ARCH}."
