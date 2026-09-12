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

# Harden apt against transient mirror flakiness. The arm64 image resolves to
# ports.ubuntu.com (amd64 uses archive.ubuntu.com), which intermittently times
# out from CI networks and would otherwise fail the very first apt-get update.
# Retrying each fetch and bounding the connect timeout lets a hiccup self-heal.
RUN printf 'Acquire::Retries "5";\nAcquire::http::Timeout "30";\nAcquire::https::Timeout "30";\n' \
    > /etc/apt/apt.conf.d/80-fastslide-retries

# g++ builds the C++ smoke, gcc the pure-C smoke, rustc the Rust smoke (all three
# language paths a downstream consumer might use), binutils supplies `nm` for the
# exported-symbol assertion, and pkg-config resolves the shipped fastslide.pc.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        g++ gcc rustc binutils pkg-config ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY *.deb ./
COPY deb_smoke.cpp deb_smoke_c.c deb_smoke.rs ./

# Install runtime + dev together so libfastslide-dev's exact-version dependency
# on libfastslide is satisfied from the local .deb files.
RUN apt-get update \
    && apt-get install -y --no-install-recommends ./*.deb \
    && rm -rf /var/lib/apt/lists/*

# Assert the runtime .so actually exports the C API. This is the cheapest, most
# specific guard against the regression where libfastslide_c's objects were
# dropped from the linkshared library, leaving a .so with no fastslide_* symbols
# (headers present, nothing to link against -- unusable from Rust / ctypes).
RUN nm -D --defined-only /usr/lib/libfastslide.so | grep -q ' T fastslide_initialize' \
    || (echo "ERROR: libfastslide.so exports no C API symbols" >&2; exit 1)

# Compile the C++ smoke against the installed SDK via pkg-config (which also
# proves the shipped fastslide.pc is correct) and run it. Only -lfastslide is
# needed: simpletiff and the other deps are statically linked into the .so.
RUN pkg-config --exists fastslide \
    && echo "fastslide.pc version: $(pkg-config --modversion fastslide)" \
    && g++ -std=c++20 -Wall -Wextra $(pkg-config --cflags fastslide) \
        deb_smoke.cpp -o /usr/local/bin/deb_smoke $(pkg-config --libs fastslide) \
    && /usr/local/bin/deb_smoke

# Compile the pure-C smoke with gcc (NOT g++): proves the C API symbols link
# from C and that the shipped fastslide/c/*.h headers are C-clean (bindgen-ready).
RUN gcc -std=c11 -Wall -Wextra $(pkg-config --cflags fastslide) \
        deb_smoke_c.c -o /usr/local/bin/deb_smoke_c $(pkg-config --libs fastslide) \
    && /usr/local/bin/deb_smoke_c

# Compile the Rust smoke with rustc, linking the runtime .so exactly the way
# rust/fastslide-sys's build.rs tells cargo to (-L native=<dir> -l dylib=fastslide).
# This reproduces the original bug report's use case.
RUN rustc --edition 2021 -L native=/usr/lib -l dylib=fastslide \
        deb_smoke.rs -o /usr/local/bin/deb_smoke_rust \
    && /usr/local/bin/deb_smoke_rust

# Exercise the CLI binary shipped in the runtime package.
RUN fastslidetool --help

CMD ["/usr/local/bin/deb_smoke"]
