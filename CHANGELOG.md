## 0.43.0

- Added vector clipping path support to the CPU page renderer.
- Clipping is applied at path termination after `W`/`W*` and restored through `q`/`Q`.
- Added renderer option `honorClippingPaths`.
- Added clipping regression coverage.

# Changelog

## 0.42.0

- Render decoded Image XObjects and inline images in the CPU page renderer.
- Support DeviceGray, DeviceRGB, and DeviceCMYK sample conversion.
- Apply soft-mask alpha bytes when available.
- Add nearest-neighbor and bilinear image sampling.
- Add configurable 1x-4x supersampling anti-aliasing with box downsampling.
- Add renderer tests for image pixels, anti-aliasing dimensions, and validation.

## 0.32.0

- Cached file size in `PdfFileInputSource` and added a single-open `ReadAll()` fast path.
- Replaced hot-path real-number parsing allocations with floating-point `std::from_chars`, retaining a compatibility fallback.
- Replaced stream-formatting-heavy object serialization with `std::to_chars` and direct buffered writes.
- Eliminated the full haystack copy for case-sensitive literal search.
- Added O(1) line-barrier checks for search matches through prefix sums.
- Added a precompiled-regex overload for repeated page/document searches.
- Extended benchmarks with case-sensitive literal and precompiled-regex workloads.
- Confirmed strict warning-clean build and full regression-suite success.

## 0.31.0

- Added ECMAScript regular-expression text search with case-insensitive matching, match limits, line-boundary policy, and geometry mapping.
- Reduced text-search allocations by pre-sizing searchable text and byte-origin buffers.
- Added per-extraction font-resource caching to avoid repeated resolver lookups for every text-showing operator.
- Reduced extraction allocations through chunk/output preallocation, single-pass UTF-8/space analysis, and move-based decoded-text storage.
- Extended benchmark coverage for literal and regular-expression search.

## 0.30.0

- Added document-level embedded files through the `/EmbeddedFiles` name tree.
- Added associated-file (`/AF`) relationships and MIME subtype serialization.
- Added page file-attachment annotations with Graph, Paperclip, PushPin, and Tag icons.
- Added memory- and filesystem-based attachment APIs, metadata, compression, removal, and validation.
- Added strict-build and integration coverage for embedded streams, file specifications, and annotations.

## 0.29.0

- Added document open actions with Fit, FitH, and XYZ destinations.
- Added non-full-screen page mode, print scaling, duplex, tray selection, and copy-count viewer preferences.
- Open-action page targets now remap when pages are inserted, removed, or moved.
- Added strict integration coverage for catalog action and print-preference serialization.


## 0.28.0

- Added document viewer preferences and catalog page layout/page mode controls.
- Added page-label number trees with decimal, Roman, alphabetic, prefix, and custom start support.
- Page-label anchors now follow page insertion, removal, and movement.
- Added strict integration coverage for generated catalog and page-label structures.

## 0.27.0

- Added named destinations using the catalog destination name tree.
- Added internal link annotations targeting named destinations.
- Added URI link annotations with optional visible borders.
- Added automatic destination page remapping during page insertion, removal, and movement.
- Added validation for duplicate destinations and unresolved internal link targets.
- Added navigation integration tests.

## 0.26.0

### Changed

- Hardened the TrueType parser and subset writer for warning-clean builds.
- Expanded UTF-8 validation into explicit, auditable control flow.
- Removed misleading single-line conditionals from sensitive font-processing code.
- Added project overview, build instructions, stability guidance, and a feature matrix.
- Confirmed strict CMake support through `PDFPP_WARNINGS_AS_ERRORS`.

## 0.25.0

### Added

- Hierarchical document bookmarks/outlines.
- Fit-page, fit-width, and XYZ destinations.
- Bookmark styling, collapsed state, and page-index remapping.

## 0.24.0

### Added

- Document information metadata writing and round-trip tests.

## 0.33.0

- Added `PdfTextSearchIndex` for repeated literal and regular-expression searches without rebuilding the searchable text on every query.
- Replaced per-byte origin records with compact chunk spans and binary-search mapping, substantially reducing search-index memory usage for large text pages.
- Precomputed the ASCII-folded searchable buffer once per reusable index.
- Added reusable-index unit tests and benchmark workloads.

## 0.41.0

Rendering foundation release.

- Added `PdfBitmap` RGBA raster target and PPM export.
- Added `PdfPageRenderer` with configurable DPI, background, crop-box selection and output limits.
- Added page rotation and current-transformation-matrix mapping.
- Added CPU rasterization for line, rectangle and flattened Bezier paths.
- Added RGB and grayscale stroke/fill state plus line-width processing.
- Added fill, stroke and combined path paint operations.
- Added an ASCII fallback text rasterizer backed by extracted text geometry.
- Added rendering validation and round-trip unit tests.
- Added Visual Studio and CMake project integration for the rendering module.

## 0.40.0

Cumulative performance release covering milestones 0.34 through 0.40.

### 0.34 — Document text index
- Added `PdfTextDocumentIndex` for lazy page extraction and document-wide literal/regex search.
- Added memory-budgeted LRU page cache and cache statistics.
- Added preload support and shared reusable `PdfTextSearchIndex` entries.

### 0.35 — Mapped input foundation
- Added `PdfMappedFileInputSource` and `PdfDocument::OpenMapped()`.
- Kept buffered file input as the default after benchmark verification showed it is faster on small/medium files.

### 0.36 — Extraction cache consolidation
- Reused page chunks, searchable text and regex/literal mapping through document-level entries.
- Preserved per-extraction font resource caching and object-cache limits.

### 0.37 — Content tokenizer hot path
- Changed content words/operators to `std::string_view` tokens.
- Added allocation-free `std::from_chars()` number parsing with compatibility fallback.
- Deferred string creation until an emitted public content event requires ownership.

### 0.38 — Search pipeline hardening
- Added document-wide searches using precompiled `std::regex`.
- Added global regex match limits across pages.
- Exposed reusable index text/chunk views for advanced callers.

### 0.39 — Output streaming foundation
- Added `PdfWriter::Save(std::ostream&, ...)` overloads.
- File saves now use the same output-stream serialization path.

### 0.40 — Benchmark and regression gate
- Added cold/warm document-index benchmark workloads.
- Added tests for mapped input, output streams, cache statistics, preload and document-wide search.
- Maintained strict warnings-as-errors builds and full regression-suite compatibility.
