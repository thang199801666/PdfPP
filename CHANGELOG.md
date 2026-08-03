## 0.59.0

- Added ICCBased image rendering: ICCBased images (N=1 gray, N=3 RGB, N=4 CMYK)
  now render through an identity transform on the profile component count, so
  they stay visible and color-reasonable instead of rendering blank.
- Added a per-pass decoded-image cache in the renderer: repeated references to
  the same image XObject (or inline images) decode the stream once per render
  instead of once per drawing operation.
- Tests: `Feature.IccBasedRendering` verifies a 2x1 ICCBased RGB image renders
  red/blue. All suites green (24 feature subtests).

## 0.58.0

- Fixed color-space detection for array-encoded image color spaces: `[ /Separation
  ... ]`, `[ /ICCBased ... ]`, `[ /DeviceN ... ]` and `[ /Indexed ... ]` all
  defaulted to `Indexed`; the resolved enum is now set correctly during image
  extraction.
- Fixed Separation rendering: the tint transform function result was multiplied
  by the tint again (double-scaling), and DeviceCMYK alternates dropped the
  black channel. Both are corrected.
- DeviceN images now parse their alternate space and shared tint function, and
  render through the same tint-transform path as Separation (Gray/RGB/CMYK
  alternates supported).
- Tests: `Feature.SeparationAndDeviceNRendering` renders a Separation image whose
  Type 2 function maps tint 0 to blue and tint 1 to red. All suites green.

## 0.57.0

- Added tiling pattern support:
  - Content processor parses `/Pattern cs/CS` and `scn/SCN` with a pattern
    name, tracking `fillPatternName`/`strokePatternName` in the text state.
  - `PdfDocument::ResolveTilingPattern` reads a `/Pattern` resource (type 1)
    and extracts `/BBox`, `/XStep`, `/YStep`, `/PaintType`, `/TilingType`,
    `/Matrix`, resources, and the tile content stream.
  - The renderer replays the tile content for every tile intersecting the
    filled region (`/Pattern cs /P1 scn ... re f` now fills with the repeated
    tile instead of a solid color).
- Tests: `Feature.TilingPatternRendering` verifies alternating red squares on
  white for a 20x20 tile with a 10x10 red fill. All suites green.

## 0.56.0

- Fixed shading resource resolution: `ResolveShading` now accepts a shading
  dictionary referenced by `/Shading` even when the referenced object is a plain
  dictionary (not only a stream), so `sh` operators with dictionary shadings
  resolve and render correctly.
- Shading paint now blends into the target bitmap (`BlendPixelInBounds`) instead
  of overwriting, so axial/radial gradients composite correctly over content.
- Soft masks with dimensions different from their image are now sampled by
  normalized coordinates instead of assuming the same pixel layout.
- Added `PdfImageInfo.softMaskWidth`/`softMaskHeight`.
- Tests: `Feature.ShadingRenderingAndSoftMask` renders an axial gradient (blue to
  red across the page) through a clip path with a differing-size soft mask. All
  suites green.

## 0.55.0

- Added optional content groups (PDF layers): `PdfWriter::AddOptionalContentGroup`
  registers named layers with initial visibility, and `PdfCanvas::BeginLayer`/
  `EndLayer` wrap drawing in `/OC <name> BDC ... EMC` marking.
- The writer now emits `/OCProperties` on the catalog (with `/OCGs`, `/D`,
  `/Order`, and `/Default`), per-page `/Resources /Properties` dictionaries, and
  validates that layers referenced by the canvas are registered.
- Tests: `Feature.OptionalContentLayers` covers registration, reuse, missing-layer
  rejection, and the saved PDF's OCG structure. All suites green.

## 0.54.0

- `PdfAnnotationEditor::FlattenAnnotations` burns annotations into the page
  content stream (using their generated `/AP /N` appearance or a native
  drawing fallback) and removes them from `/Annots`, with an optional subtype
  filter for partial flattening.
- Annotation replies: `PdfAnnotation.inReplyTo` and `PdfAnnotation.replyType`
  write the `/IRT` and `/RT` keys so annotations form reply threads.
- Linked `/Popup` annotations are emitted automatically when
  `PdfAnnotation.hasPopup` is set (with `/Parent` back-reference).
- Tests: flattening (full + filtered), reply threads, and popup linkage are
  covered by `API.AdvancedAnnotationsAndXfdf`. All suites green.

## 0.53.0

- Added FreeText, Ink, Polygon, Polyline, Square, Circle and Stamp annotation
  types to `PdfAnnotationEditor` with per-type fields (ink paths, vertices,
  interior color, border width, line-end styles, rotation, stamp name, text
  alignment).
- `PdfAnnotationEditor::RemoveAnnotations` removes annotations by page with an
  optional subtype filter; `UpdateAnnotationContents` rewrites the contents and
  title of matching annotations.
- `PdfAnnotationEditor::GenerateAppearances` writes deterministic `/AP /N` Form
  XObject appearance streams for every drawable annotation type using native
  PDF graphics operators.
- Added XFDF export/import (`PdfXfdf`): exports page annotations to the Adobe
  XFDF XML schema and imports an XFDF file back into a PDF via the annotation
  editor.
- Fixed annotation serialization bugs: line-end names now carry the `/` name
  prefix, and interior-color/border defaults no longer leak into unrelated
  annotation types.
- Tests: `API.AdvancedAnnotationsAndXfdf` covers the new types, removal,
  update, appearance generation and the XFDF round trip. All suites green.

## 0.52.0

- Reorganized the unit-test suite by domain: the monolithic `RunCoreTests()`
  and the large reader/writer/security integration functions were split into
  small, named test cases so a failure pinpoints the exact component.
- Extended `TestRunner.hpp` with `PDFPP_TEST_CHECK_NEAR` and
  `PDFPP_TEST_EXPECT_THROWS` helpers; added a shared `TestHelpers.hpp` for
  deterministic CFF and LZW fixtures.
- Core-domain cases now live in `ObjectAndRenderingTests`, `CffAndFilterTests`,
  `ContentTextFontTests`, and `PdfAValidationTests`; `CoreTests.cpp` is a thin
  registry around `main()`.
- Added coverage for `PdfTextSearchIndex`, search options, and `PdfDisplayList`
  replay/clear.
- All suites green: 23 top-level test cases across core, reader, writer, API,
  feature, security, and validation groups.

## 0.51.0

- Added differential text-extraction validation against MuPDF.
- `compare_text.py` compares per-page extracted text between Pdf++ and MuPDF by token overlap, reporting low-overlap pages without failing the run for complex layouts.
- Added `PdfPP.GenCorpus`, a deterministic corpus generator covering text, vector paths, images, transparency, and multi-page documents; fixtures are written to `tests/corpus/generated/` and redistributable under the repository license.
- All self-owned corpus fixtures now validate against MuPDF through the render and text scripts.

## 0.50.0

- Added differential rendering validation against MuPDF.
- `PdfPP.Inspect` now renders every page to PPM with a machine-readable `render <page> <w> <h> <dark>` summary.
- `compare_render.py` renders a selected page with both Pdf++ and MuPDF (via `mutool` or PyMuPDF) and compares dimensions and dark-pixel coverage; dimension mismatches are hard failures, coverage deltas beyond tolerance are reported as warnings.
- Fixed UTF-8 subprocess decoding in the validation scripts on Windows.
- Documented the test-corpus license policy for redistributable fixtures.

## 0.49.0

- Added line dash pattern support to the CPU renderer.
- The content processor now parses the `d` operator into a `SetDashPattern` event carrying the pattern array and phase.
- The renderer applies dash on/off alternation across subpaths with phase offset, including within transparency groups and graphics-state save/restore.
- Added `PdfContentEventType::SetDashPattern` and `dashPattern`/`dashPhase` graphics-state fields.
- Added content-parse and pixel-level rendering coverage for dash patterns.

## 0.48.0

- Added four additional Clang libFuzzer targets alongside the existing reader harness.
- `PdfPP.FuzzContent` exercises content-stream and graphics/text operator parsing.
- `PdfPP.FuzzFilter` decodes Flate, ASCIIHex, ASCII85, RunLength, and LZW streams.
- `PdfPP.FuzzCffFont` and `PdfPP.FuzzTrueTypeFont` parse embedded font programs and walk glyph outlines.
- All harnesses run under AddressSanitizer/UndefinedBehaviorSanitizer and accept normal parser exceptions as expected input.

## 0.47.0

- Added practical PDF/A-1/2/3/4 conformance validation.
- `PdfConformanceProfile` now covers A-1/2/3 conformance levels (A, B, U) plus PDF/A-4.
- The validator checks the file version header, forbids encryption, verifies the XMP metadata stream's `pdfaid:part`/`conformance`, requires a `/GTS_PDFA1` output intent, validates embedded TrueType/CFF fonts, enforces ToUnicode for level U and tagged structure for level A, rejects transparency for PDF/A-1, and flags forbidden annotation subtypes.
- Added regression coverage for PDF/A pass/fail scenarios.

## 0.46.0

- Added an external digital-signature foundation (`PdfSignatureManager`).
- `PrepareForSigning` adds a `/Sig` field with widget annotation and a signature dictionary, then writes a prepared file whose `/Contents` is a zero-filled placeholder.
- ByteRange handling: the prepared file's `/ByteRange` is patched in place and the exact digest input (the two byte ranges) is exposed for an external signer.
- `ApplySignature` writes the produced signature value (hex, zero-padded) into the placeholder while preserving ByteRange validity; `Sign` wraps prepare/sign/apply in one callback-based call.
- `GetSignatures` inspects signature fields, `/V` dictionaries, ByteRange, and applied contents.
- Added regression coverage for the prepare/sign/apply/inspect round trip.

## 0.45.0

- Added embedded CFF font parsing: charset (formats 0/1/2), CharStrings INDEX, Private DICT default/nominal widths, and local/global subrs.
- Added a bounded Type 2 charstring interpreter producing move/line/cubic glyph outlines with width extraction.
- The CPU renderer now rasterizes embedded CFF/Type 2 glyphs (Type1C, CIDFontType0C, OpenType) with cubic flattening; identity-encoded CID fonts map character codes to glyphs directly.
- Font descriptors of composite fonts are now resolved through the descendant CID font when missing on the Type0 wrapper.
- Added regression coverage for the CFF parser, the charstring interpreter, and embedded-CFF glyph rendering.

## 0.44.0

- Added transparency-group boundary discovery and compositing to the CPU renderer.
- Form XObjects carrying `/Group << /S /Transparency >>` now render into an offscreen layer and composite back with the group's blend mode, alpha, isolated, and knockout flags.
- BDC/EMC marked content with an inline or name-referenced transparency group emits dedicated begin/end group events.
- Added `BeginTransparencyGroup`, `EndTransparencyGroup`, `BeginMarkedContent`, and `EndMarkedContent` content event types with group metadata.
- The renderer now replays vector paths and images in a single content-order pass so transparency groups and z-order are preserved.
- Added rendering regression coverage for Form XObject groups and marked-content groups.

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
