Java Packages and Releases
==========================

FastSlide ships a small Java API plus per-platform native libraries as JARs.
They are built with Bazel and published as **GitHub Release assets** on
``NKI-AI/fastslide``, where downstream projects (notably the
`QuPath FastSlide extension <https://github.com/NKI-AI/qupath-extension-fastslide>`_)
consume them anonymously through a Gradle ``ivy``/url repository.

Artifacts and coordinates
-------------------------

A release contains:

- ``fastslide-java-<version>.jar`` -- platform-independent wrapper (the Java
  API, including ``dev.aifo.fastslide.tools.FastSlideTool``). Coordinate:
  ``dev.aifo:fastslide-java:<version>``.
- ``fastslide-native-<version>-<os>-<arch>.jar`` -- one classifier JAR per
  platform, carrying the native shared library at
  ``META-INF/native/<os>-<arch>/``. Coordinate:
  ``dev.aifo:fastslide-native:<version>:<os>-<arch>``.
- ``SHA256SUMS`` -- checksums for every JAR.

Supported ``<os>-<arch>`` classifiers: ``linux-x86_64``, ``linux-aarch64``,
``darwin-x86_64``, ``darwin-aarch64``, ``windows-x86_64``, ``windows-aarch64``.

The version is read from ``package/versions.json``. The release **tag equals the
bare version** (e.g. ``0.7.0``) so the consumer's ``[revision]`` pattern resolves
directly to ``<tag>/<file>``.

How a consumer wires it up
--------------------------

A Gradle build adds an ``ivy`` repository whose layout maps a coordinate to a
release asset URL, then declares ordinary dependencies:

.. code-block:: kotlin

   repositories {
       ivy {
           url = uri(
               providers.gradleProperty("fastslideRepoUrl")
                   .getOrElse("https://github.com/NKI-AI/fastslide/releases/download")
           )
           patternLayout { artifact("[revision]/[module]-[revision](-[classifier]).[ext]") }
           metadataSources { artifact() }
           content { includeGroup("dev.aifo") }
       }
   }

   dependencies {
       implementation("dev.aifo:fastslide-java:0.7.0")
       listOf("linux-x86_64", "linux-aarch64", "darwin-x86_64",
              "darwin-aarch64", "windows-x86_64", "windows-aarch64")
           .forEach { runtimeOnly("dev.aifo:fastslide-native:0.7.0:$it") }
   }

The native library loads itself off the classpath at runtime
(``NativeLoader.tryLoadFromClasspath`` reads ``META-INF/native/<os>-<arch>/``),
so no native path configuration is needed.

.. _java-local-release:

Building and testing locally (no GitHub)
----------------------------------------

The build, smoke-test, and publish steps live in Python scripts under ``tools/``
that CI merely invokes, so the entire publish -> consume loop can be exercised
offline.

1. **Build the host platform** (use your own ``--platform`` key):

   .. code-block:: bash

      python3 tools/build_java_artifacts.py --platform darwin_aarch64

   This writes the wrapper and the classifier JAR to ``artifacts/jars/``.

2. **Smoke-test the shipped bits** the same way a consumer loads them -- the
   wrapper + classifier JARs on the classpath, running ``FastSlideTool info``
   against the bundled sample (``tests/test_data/CMU-1-Small-Region.svs``):

   .. code-block:: bash

      python3 tools/smoke_test_java.py --platform darwin_aarch64

3. **Stage a local release** in the exact ``ivy`` layout a consumer expects:

   .. code-block:: bash

      python3 tools/publish_java_artifacts.py --dest local --out-dir /tmp/fastslide-release
      # -> /tmp/fastslide-release/0.7.0/fastslide-java-0.7.0.jar, ...

4. **Build the consumer against the local release** by overriding the repo URL:

   .. code-block:: bash

      ./gradlew fatJar -PfastslideRepoUrl=file:///tmp/fastslide-release

Because step 3 produces the identical layout CI publishes, a green local run is
a faithful preview of the real release.

Publishing a GitHub release
---------------------------

Releases are cut by the **manual** ``Release Java artifacts`` workflow
(``.github/workflows/release-java.yml``); it never fires automatically on a
version bump. To publish:

1. Bump ``version`` in ``package/versions.json``.
2. From the ``NKI-AI/fastslide`` repo, run the workflow (Actions tab ->
   *Release Java artifacts* -> *Run workflow*). Set ``release: true`` for a final
   release, or leave it ``false`` for a prerelease/snapshot (tag
   ``<version>-dev.<shortsha>``). The ``platforms`` input lets you build a
   subset for a cheap trial run.

What the workflow does
~~~~~~~~~~~~~~~~~~~~~~~

The workflow runs in two phases -- **build** (produce each classifier JAR where
it can be built) and **smoke** (run every JAR on its real target OS/arch) --
followed by an aggregate **release** job:

.. mermaid::

   flowchart TB
       dispatch["workflow_dispatch (release bool)"] --> build
       subgraph build [build: produce classifier JARs]
           blx["linux_x86_64 / ubuntu-24.04 (native)"]
           bla["linux_arm64 / ubuntu-24.04-arm (native)"]
           bmx["darwin_x86_64 / macos-13 (native)"]
           bma["darwin_aarch64 / macos-14 (native)"]
           bwx["windows_x86_64 / ubuntu-24.04 (cross, Zig)"]
           bwa["windows_arm64 / ubuntu-24.04 (cross, Zig)"]
       end
       subgraph smoke [smoke: run JARs on the real target runner]
           slx["linux_x86_64 / ubuntu-24.04"]
           sla["linux_arm64 / ubuntu-24.04-arm"]
           smx["darwin_x86_64 / macos-13"]
           sma["darwin_aarch64 / macos-14"]
           swx["windows_x86_64 / windows-2022"]
           swa["windows_arm64 / windows-11-arm"]
       end
       build --> smoke
       smoke --> rel["release job (needs all): provenance + gh release"]
       rel --> ivy["consumers resolve dev.aifo:fastslide-* anonymously"]

Why the split:

- **macOS / Linux are built natively.** FastSlide does **not** cross-compile to
  macOS, so darwin JARs must be produced on macOS runners; Linux is built on
  Linux.
- **Windows is cross-compiled on Linux** (hermetic Zig toolchain). Windows
  cannot act as the Bazel *host* here: ``aspect_rules_py`` / ``rules_uv`` reject
  a Windows host (``Unsupported platform windows``), which aborts analysis of
  even pure Java targets. Building with a Linux host (target = windows) avoids
  that entirely.
- **Smoke tests always run on the real target runner** (including native
  Windows). They need only a JDK and the published JARs -- no Bazel -- so they
  validate the exact artifact a consumer would load, regardless of where it was
  built.

The build and smoke matrices are ``fail-fast: false``; the ``release`` job
``needs:`` the smoke phase, so a release is only cut when every selected
platform both built and smoke-tested cleanly. Released JARs additionally get a
SLSA build-provenance attestation.

.. note::

   ``NKI-AI/fastslide`` must be public for anonymous consumption. The
   ``windows-11-arm`` smoke leg is the least battle-tested (JDK availability on
   Windows arm64); if it is flaky, omit ``windows_arm64`` from the ``platforms``
   input -- the other platforms are unaffected.
