Packages and Releases
=====================

A FastSlide release publishes two artifact families, all built with Bazel and
tied together by a single GitHub Release on ``NKI-AI/fastslide``:

- **Python wheels** -- published to **PyPI** (``pip install fastslide``) and also
  attached to the GitHub Release.
- **Java artifacts** -- a small Java API plus per-platform native libraries as
  JARs, published as **GitHub Release assets** and consumed anonymously by
  downstream projects (notably the
  `QuPath FastSlide extension <https://github.com/NKI-AI/qupath-extension-fastslide>`_)
  through a Gradle ``ivy``/url repository.

Java artifacts and coordinates
------------------------------

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
``darwin-x86_64``, ``darwin-aarch64``, ``windows-x86_64``.

.. note::

   ``windows-aarch64`` is **not** published. The only available cross toolchain
   (Zig) emits an arm64 Windows PE that the loader rejects (``LoadLibraryEx``
   error 193), and we cannot build on a Windows host because ``rules_uv`` has no
   Windows host support. It will be added once a native arm64 Windows build is
   possible.

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
              "darwin-aarch64", "windows-x86_64")
           .forEach { runtimeOnly("dev.aifo:fastslide-native:0.7.0:$it") }
   }

The native library loads itself off the classpath at runtime
(``NativeLoader.tryLoadFromClasspath`` reads ``META-INF/native/<os>-<arch>/``),
so no native path configuration is needed.

Python wheels
-------------

The Python package is distributed as a single stable-ABI (abi3) wheel per
platform, tagged ``cp312-abi3`` and built against CPython's stable ABI so it
runs unchanged on every CPython >= 3.12. Wheels are built with Bazel and
**published to PyPI**, so consumers just::

   pip install fastslide

The same wheels are attached to the GitHub Release for convenience. Wheels are
built for ``linux``/``darwin`` (x86_64 + aarch64) and ``windows`` (x86_64);
``windows_arm64`` is skipped (``rules_python`` has no ``py_cc`` toolchain for it,
matching the Java side). Build them locally with ``tools/build_wheels.py``
(see below).

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

To build wheels locally (optionally narrowing the platform/Python matrix)::

   python3 tools/build_wheels.py --platform darwin_aarch64
   # -> artifacts/wheels/*.whl

``publish_java_artifacts.py`` also attaches any wheels in ``artifacts/wheels``
to the GitHub Release (its ``--wheels-dir`` defaults there); for a local
``--dest local`` stage only the Java JARs are written.

Publishing a release
--------------------

Releases are cut by the **manual** ``Release FastSlide`` workflow
(``.github/workflows/release.yml``); it never fires automatically on a version
bump. To publish:

1. Bump ``version`` in ``package/versions.json``.
2. From the ``NKI-AI/fastslide`` repo, run the workflow (Actions tab ->
   *Release FastSlide* -> *Run workflow*). Set ``release: true`` for a final
   release (tag ``<version>``, marked latest, **wheels published to PyPI**), or
   leave it ``false`` for a prerelease/snapshot (tag ``<version>-dev.<shortsha>``,
   GitHub prerelease, **wheels published to TestPyPI** so the publish path is
   exercised without touching the real index). The ``platforms`` input lets you
   build a subset for a cheap trial run.

You do **not** create the git tag or write release notes by hand: the workflow
creates the tag via ``gh release create`` and auto-generates the notes
(``--generate-notes``) from merged PRs since the previous tag.

What the workflow does
~~~~~~~~~~~~~~~~~~~~~~~

The workflow fans out into per-platform **build** jobs (Java JARs and Python
wheels) plus on-target **smoke** jobs, then converges on PyPI publishing and a
single aggregate **GitHub Release**:

.. mermaid::

   flowchart TB
       dispatch["workflow_dispatch (release bool)"] --> bj & bw
       subgraph bj [build-java: classifier JARs]
           blx["linux_x86_64 / ubuntu-24.04"]
           bla["linux_arm64 / ubuntu-24.04-arm"]
           bmx["darwin_x86_64 / macos-14 (cross arm->x86_64)"]
           bma["darwin_aarch64 / macos-14"]
           bwx["windows_x86_64 / ubuntu-24.04 (cross, Zig)"]
       end
       subgraph sj [smoke-java: run JARs on the real target OS/arch]
           slx["linux x86_64/arm64"]
           smx["darwin x86_64 (Rosetta) / aarch64"]
           swx["windows_x86_64 / windows-2022"]
       end
       subgraph bw [build-wheels: one cp312-abi3 wheel per platform]
           wl["linux x86_64/arm64"]
           wm["darwin x86_64/aarch64"]
           ww["windows_x86_64"]
       end
       subgraph sw [smoke-wheels: import + open sample, every platform x cp312-cp314]
           swl["linux x86_64/arm64"]
           swm["darwin x86_64 (Rosetta) / aarch64"]
           sww["windows_x86_64 / windows-2022"]
       end
       bj --> sj
       bw --> sw
       sw --> pypi["publish wheels (Trusted Publishing): PyPI if release, else TestPyPI"]
       sj --> rel
       sw --> rel
       pypi --> rel["github-release (needs all): provenance + gh release (jars + wheels + SHA256SUMS, auto notes)"]
       rel --> consumers["pip install fastslide / Gradle ivy resolves dev.aifo:fastslide-*"]

Why the split:

- **macOS uses only the arm64 runner.** FastSlide does **not** cross-compile to
  macOS from another OS, but the Apple toolchain on an arm64 Mac targets
  ``x86_64`` cheaply, so ``darwin_x86_64`` is cross-compiled on ``macos-14`` (the
  Intel runner is slow and frequently unavailable). Its smoke test also runs on
  ``macos-14`` under **Rosetta**, using an x86_64 JDK so the JVM and the x86_64
  ``.dylib`` it loads are actually executed (emulated), not skipped. Linux is
  built natively on Linux.
- **Windows x86_64 is cross-compiled on Linux** (hermetic Zig toolchain).
  Windows cannot act as the Bazel *host* here: ``aspect_rules_py`` / ``rules_uv``
  reject a Windows host (``Unsupported platform windows``), which aborts analysis
  of even pure Java targets. Building with a Linux host (target = windows) avoids
  that entirely. ``windows_arm64`` is not shipped (Zig emits an arm64 PE the
  Windows loader rejects with error 193, and a native Windows host build is
  blocked by ``rules_uv``).
- **Smoke tests always run on the real target runner** (including native
  Windows). The Java smoke needs only a JDK + the JARs; the wheel smoke
  (``smoke-wheels``) installs the built wheel into a fresh ``uv`` venv on each
  platform x CPython (3.12--3.14), then ``import fastslide`` and opens the
  bundled sample (``tools/smoke_test_python.py``). Importing the one abi3 wheel
  on every version proves the stable-ABI tag actually loads. No Bazel -- so both
  validate
  the exact artifact a consumer would load. PyPI/TestPyPI publishing
  ``needs:`` the wheel smoke, so broken wheels never reach an index.

All matrices are ``fail-fast: false``; the ``github-release`` job ``needs:`` the
smoke phase, the wheel builds, and (on a full release) the PyPI publish, so a
release is only cut when every selected platform built, smoke-tested, and
published cleanly. Every released JAR and wheel gets a SLSA build-provenance
attestation.

.. note::

   ``NKI-AI/fastslide`` must be public for anonymous Java consumption.

   **PyPI prerequisite (one-time).** Publishing wheels uses `Trusted Publishing
   <https://docs.pypi.org/trusted-publishers/>`_ (OIDC, no API token). Add a
   trusted publisher on **both** indexes for the project (owner ``NKI-AI``,
   repository ``fastslide``, workflow ``release.yml``):

   - On `PyPI <https://pypi.org/manage/account/publishing/>`_ with environment
     ``pypi`` (used by full releases).
   - On `TestPyPI <https://test.pypi.org/manage/account/publishing/>`_ with
     environment ``testpypi`` (used by snapshot dry-runs).

   Both publish jobs use ``skip-existing`` so a re-run of the same version is a
   no-op rather than a failure.
