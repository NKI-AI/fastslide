#!/usr/bin/env bash
# Smoke-test the FastSlide Debian packages by installing them on THIS system.
#
# Usage:
#   package/smoke_deb.sh libfastslide_<ver>_<arch>.deb libfastslide-dev_<ver>_all.deb
#
# Run it on Ubuntu 24.04 (the release CI runs it on the ubuntu-24.04 runner
# that built the debs). Elsewhere, run it in a throwaway ubuntu:24.04
# container with g++, pkg-config and curl installed. It needs sudo (or root),
# because it installs packages.
#
# It:
#   1. downloads the dependencies listed in deb_smoke_deps.txt over HTTPS and
#      checks their SHA-256 (no apt, so no apt mirror is involved);
#   2. installs everything with dpkg, which fails on any unmet dependency;
#   3. checks that libfastslide.so has no unresolved shared libraries;
#   4. compiles deb_smoke.cpp with the system compiler against ONLY the
#      installed SDK (via pkg-config, which also checks fastslide.pc) and runs
#      it, then runs the shipped fastslidetool CLI.
set -euo pipefail

if [[ $# -eq 0 ]]; then
  sed -n '2,6p' "$0" >&2
  exit 2
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH="$(dpkg --print-architecture)"
SUDO=""
if [[ "${EUID}" -ne 0 ]]; then
  SUDO="sudo"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

echo "==> Downloading the pinned dependencies for ${ARCH}"
deps=()
while read -r arch sha256 url; do
  [[ "${arch}" == "${ARCH}" ]] || continue
  file="${WORK}/${url##*/}"
  curl -fsSL --retry 3 --retry-all-errors --connect-timeout 15 --max-time 120 \
    -o "${file}" "${url}"
  echo "${sha256}  ${file}" | sha256sum --check --strict
  deps+=("${file}")
done < <(grep -v -E '^[[:space:]]*(#|$)' "${HERE}/deb_smoke_deps.txt")

echo "==> Installing $* ${deps[*]##*/}"
${SUDO} dpkg -i "${deps[@]}" "$@"

echo "==> Checking libfastslide.so's shared-library dependencies"
so="$(dpkg -L libfastslide | grep -E '/libfastslide\.so(\.|$)' | head -n 1)"
ldd "${so}"
if ldd "${so}" | grep -q 'not found'; then
  echo "error: ${so} has unresolved shared libraries" >&2
  exit 1
fi

echo "==> Compiling and running deb_smoke.cpp against the installed SDK"
echo "fastslide.pc version: $(pkg-config --modversion fastslide)"
# shellcheck disable=SC2046 # pkg-config output is a word list
g++ -std=c++20 -Wall -Wextra $(pkg-config --cflags fastslide) \
  "${HERE}/deb_smoke.cpp" -o "${WORK}/deb_smoke" $(pkg-config --libs fastslide)
"${WORK}/deb_smoke"

echo "==> Running fastslidetool"
fastslidetool --help

echo "==> Debian package smoke test passed for ${ARCH}."
