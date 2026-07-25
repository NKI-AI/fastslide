# Standalone smoke test for the FastSlide Debian packages.
#
# The build context must contain the built *.deb files plus this directory's
# deb_smoke.cpp. It installs the packages with apt (so transitive deps resolve),
# then compiles the smoke program with the SYSTEM compiler against ONLY the
# installed package (headers in /usr/include, libs in /usr/lib) and runs it.
#
# The base image matches the release CI builder (ubuntu-24.04) so the prebuilt
# .so's glibc/libstdc++ ABI lines up with the compiler used here.
#
#   docker build --platform linux/amd64 -t fastslide-deb-smoke .
FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ pkg-config ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY *.deb ./
COPY deb_smoke.cpp ./

# Install runtime + dev together so libfastslide-dev's exact-version dependency
# on libfastslide is satisfied from the local .deb files.
RUN apt-get update \
    && apt-get install -y --no-install-recommends ./*.deb \
    && rm -rf /var/lib/apt/lists/*

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
