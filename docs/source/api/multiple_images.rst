Multiple Images per Slide
=========================

A slide file is a **container**: most formats hold exactly one navigable
image pyramid, but some store several. FastSlide models this with two
levels:

- :cpp:class:`fastslide::SlideReader` (Python: ``FastSlide``) — the file /
  container. Owns metadata, file handles, the cache, and any associated
  images (thumbnails, label, macro).
- :cpp:class:`fastslide::SlideImage` (Python: ``SlideImageView``) — one
  navigable pyramid inside the container. Has its own dimensions, pyramid
  levels, MPP and channel layout, and exposes ``read_region`` /
  ``ReadRegion``.

Single-image readers (SVS, MRXS, QPTIFF, NDPI, ...) keep working unchanged:
``slide.images`` has length ``1`` and forwards back to the reader through a
``SelfImageView`` adapter, so callers that only use ``slide.read_region``
never see the multi-image machinery.

Multi-image example: Olympus VSI
--------------------------------

Olympus VSI stores a small **navigator** pyramid plus one
high-resolution **region** pyramid per imaged area on the slide. Both are
exposed via ``slide.images``:

.. code-block:: python

   import fastslide

   slide = fastslide.FastSlide.from_file_path("OS-1.vsi")

   # Top-level properties point at the primary (main scan) image:
   print(slide.dimensions)      # e.g. (67072, 77312)
   print(slide.images.primary)  # SlideImageView name='region 0'

   # All images, including the navigator:
   for image in slide.images:
       print(image.index, image.name, image.dimensions, image.level_count)
   # 0 navigator   (7168, 13312) 6
   # 1 region 0    (67072, 77312) 9

   # Read a tile from the navigator specifically:
   navigator = slide.images[0]
   thumb = navigator.read_region(location=(0, 0), level=0, size=(256, 256))

C++ API
-------

.. code-block:: cpp

   auto reader_or = fastslide::GetGlobalRegistry().CreateReader("OS-1.vsi");
   auto& reader = *reader_or.value();

   const int n = reader.GetImageCount();
   const int primary = reader.GetPrimaryImageIndex();
   for (int i = 0; i < n; ++i) {
     auto image_or = reader.GetImage(i);
     const fastslide::SlideImage& image = *image_or.value();
     // image.GetName(), image.GetLevelCount(), image.ReadRegion(...)
   }

   // Top-level forwarding: reader.ReadRegion(...) is equivalent to
   // reader.GetImage(reader.GetPrimaryImageIndex())->ReadRegion(...).

``images`` vs ``associated_images``
-----------------------------------

These are *different* concepts and intentionally separated:

- ``slide.images`` — full **navigable pyramids**. Multi-level, support
  ``read_region`` with arbitrary coordinates, used for actual analysis.
- ``slide.associated_images`` — single-resolution **thumbnails / overviews**
  embedded in the file (label, macro, thumbnail). Returned as a single
  decoded ``Image``, no pyramid, no random access.

If you wonder which one to use: if you would put a sliding window over it,
it is in ``slide.images``; if you only need a small reference picture, it
is in ``slide.associated_images``.

Threading and lifetimes
-----------------------

``SlideImage`` / ``SlideImageView`` are **stateless**. They do not own the
reader; the reader owns them. Multiple threads may call ``read_region`` on
the same or different
images concurrently. In Python, ``SlideImageView.read_region`` releases the
GIL for the duration of the read so Python threads can perform overlapping
reads in parallel.

Closing a ``FastSlide`` invalidates every ``SlideImageView`` that was
handed out from it — subsequent calls raise ``RuntimeError``.
