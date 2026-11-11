# Standalone smoke test for the FastSlide Debian packages.
#
# The build context must contain the built *.deb files, this directory's
# deb_smoke.cpp, and the .deb files of every dependency the base image lacks
# (the release workflow downloads those on the runner with `apt-get download`).
# It installs the packages with apt, then compiles the smoke program with the
# SYSTEM compiler against ONLY the installed package (headers in /usr/include,
# libs in /usr/lib) and runs it.
#
# Nothing here uses the network: apt installs only the local .deb files, so
# the image builds with --network=none. GitHub-hosted
# runners stopped giving containers working DNS in late 2026, and the Ubuntu
# mirrors were flaky from CI containers before that.
#
# The base image is Ubuntu 24.04 with its build toolchain (g++, pkg-config)
# preinstalled. It matches the release CI builder (ubuntu-24.04), so the
# prebuilt .so's glibc/libstdc++ ABI lines up with the compiler used here.
#
#   docker build --network=none --platform linux/amd64 -t fastslide-deb-smoke .
FROM buildpack-deps:noble

WORKDIR /work
COPY *.deb ./
COPY deb_smoke.cpp ./

# Install runtime + dev (and the downloaded dependencies) together, so
# libfastslide-dev's exact-version dependency on libfastslide and its other
# dependencies are satisfied from the local .deb files only. The image has no
# apt lists and the build has no network, so a dependency that is neither in
# the image nor in the context fails here. (apt's --no-download would say the
# same, but it rejects local .deb files.)
RUN apt-get install -y --no-install-recommends ./*.deb

# Compile against the installed SDK via pkg-config (which also proves the
# shipped fastslide.pc is correct) and run the smoke program. Only -lfastslide
# is needed: simpletiff and the other deps are statically linked into the .so.
RUN pkg-config --exists fastslide \
    && echo "fastslide.pc version: $(pkg-config --modversion fastslide)" \
    && g++ -std=c++20 -Wall -Wextra $(pkg-config --cflags fastslide) \
        deb_smoke.cpp -o /usr/local/bin/deb_smoke $(pkg-config --libs fastslide) \
    && /usr/local/bin/deb_smoke

# Exercise the CLI binary shipped in the runtime package.
RUN fastslidetool --help

CMD ["/usr/local/bin/deb_smoke"]
