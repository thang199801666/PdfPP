# Pdf++ Development Roadmap

## Goal

Build a public, freely usable C++ PDF library with core capabilities comparable
to iText Core and rendering capabilities approaching MuPDF. Add-ons such as
HTML conversion, OCR, advanced script shaping, redaction suites and XFA are
secondary scope.

## Phase 1: PDF Core

- Complete display-list replay for paths, text and images.
- Preserve graphics state and z-order during replay.
- Complete Form XObject recursion and resource scopes.
- Harden PDF 1.x, PDF 1.7 and PDF 2.0 object handling.
- Complete xref streams, object streams and incremental updates.
- Improve malformed-file recovery and error policy.
- Preserve references when copying, merging, splitting and reordering pages.
- Add parser, writer and recovery fuzzing.
- Clang libFuzzer targets cover reader (xref/object/stream parsing), content-stream processing, stream filters, and embedded CFF/TrueType font parsing with ASan/UBSan.

## Phase 2: Graphics and Rendering

- Complete line dash, cap, join and miter behavior.
- Line dash patterns are parsed (`d` operator) and applied by the CPU renderer, including phase offset and dash on/off alternation across subpaths; line cap, join, and miter behavior are implemented.
- Implement blend modes, transparency groups, soft masks and knockout.
- Bitmap batch blending and additional blend-mode coverage are implemented.
- Transparency group bitmap compositing supports isolated clearing, knockout replacement, and Form boundary discovery. The CPU renderer detects `/Group /S /Transparency` on Form XObjects and BDC/EMC marked content, renders group content into an offscreen layer, and composites with the group's blend mode, alpha, isolated, and knockout flags.
- Indexed image color spaces now carry palette metadata and render 1/2/4/8-bit samples; Separation has a limited alternate-space fallback pending tint-transform evaluation.
- Shading operator `sh` now emits a dedicated `PaintShading` event; axial/radial resource evaluation remains the next renderer step.
- Axial and radial shading are rendered by the CPU renderer: `ResolveShading` handles both stream and dictionary shading resources, and gradient paint blends into the page (blue→red axial gradients verified through clip paths).
- Added reusable Type 2 exponential PDF function evaluator for future shading and tint-transform evaluation.
- Added reusable axial shading sampler with domain and extend handling; renderer resource integration remains.
- Added sampled (Type 0) and stitched (Type 3) function foundations plus ICCBased profile and DeviceN component metadata extraction.
- Added bounded Type 4 calculator function support with a restricted arithmetic operator set and stack/token limits.
- Added embedded Type1/CFF/OpenType program subtype detection and explicit conformance diagnostics for unsupported native rasterization.
- Add DeviceGray, DeviceRGB, DeviceCMYK and ICCBased color spaces.
- DeviceGray/RGB/CMYK and ICCBased images all render; ICCBased uses an identity transform on the profile's component count (N=1/3/4).
- Add Indexed, Separation and DeviceN color spaces.
- Separation and DeviceN images render through a tint-transform function into Gray/RGB/CMYK alternate spaces; array-encoded color spaces are detected correctly (not misread as Indexed).
- Implement tiling patterns, shadings and output intents.
- Tiling patterns are parsed (`/Pattern cs` + `scn/SCN`) and rendered: the tile content stream is replayed for every tile intersecting the filled region, honoring `/BBox`, `/XStep`, `/YStep`, and `/Matrix`.
- Improve image masks, interpolation, downsampling and caching.
- The renderer caches decoded images per object number within a render pass and supersamples via `Downsample` when `antiAliasSamples > 1`.
- Add rendering comparison tests against independent reference renderers.

## Phase 3: Fonts, Text and Layout

- Add Type1, CFF, CIDFont and OpenType support.
- Embedded CFF fonts (Type1C, CIDFontType0C, OpenType with CFF outlines) are parsed through charset/CharStrings/Private DICTs, and a bounded Type 2 charstring interpreter produces glyph outlines that the CPU renderer rasterizes with cubic flattening. Identity-encoded CID fonts map character codes to glyphs directly.
- Improve font fallback, kerning, vertical writing and subsetting.
- `kern` table parsing with cached kerning pairs, and `PdfCanvas::ShowTextUtf8WithFallback` for glyph-coverage fallback across fonts.
- Add Unicode grapheme handling, bidirectional text and complex shaping.
- `PdfTextLayout` provides UAX #29 grapheme clustering and simplified UAX #9 bidi reordering.
- Add paragraph, table, list, column and page-flow layout primitives.
- `PdfDocumentLayout` flows paragraphs with automatic page breaks, draws bullet/numbered lists, multi-column text, and page headers/footers with page numbers.
- Add headers, footers, page breaks and image placement.
- Keep text extraction geometry and rendering metrics consistent.

## Phase 4: Document Features

- Complete AcroForm widgets, appearances, flags and calculations.
- AcroForm calculation scripts (`/AA /C`) are evaluated with a restricted arithmetic parser and the recalculated `/V` values are written back.
- Add XFDF import/export and reliable flattening.
- `PdfXfdf` exports page annotations to the Adobe XFDF XML schema and imports an XFDF file back into a PDF through `PdfAnnotationEditor`.
- Add FreeText, Stamp, Ink, Polygon and Polyline annotations.
- `PdfAnnotationEditor` supports FreeText, Ink, Polygon, Polyline, Square, Circle and Stamp types (ink paths, vertices, interior color, line-end styles, rotation, stamp names), plus `RemoveAnnotations`, `UpdateAnnotationContents`, and `GenerateAppearances` which writes `/AP /N` Form XObject appearances.
- Add annotation appearances, replies, popups and flattening.
- `PdfAnnotationEditor` flattens annotations into the page content stream (full or filtered), links replies via `/IRT`/`/RT`, emits `/Popup` annotations with `/Parent` back-references, and generates `/AP /N` appearance streams.
- Complete outlines, destinations, actions, page labels and optional content.
- Optional content (layers) is supported end-to-end: `PdfWriter::AddOptionalContentGroup` registers named `/OCG` groups, `PdfCanvas::BeginLayer`/`EndLayer` mark content, and the catalog carries `/OCProperties` with default visibility.
- Improve attachments, portfolios and embedded-file relationships.
- `PdfWriter::SetPortfolio` writes a catalog `/Collection` entry (view mode + title) grouping embedded files into a portfolio shell.

## Phase 5: Compliance and Security

- Implement external digital-signature callbacks and ByteRange handling.
- External signing foundation: `PdfSignatureManager` creates a /Sig field and signature dictionary, prepares a file with a zero-filled `/Contents` placeholder, computes the `/ByteRange` over the exact bytes an external signer must digest, and writes the produced signature value back into the placeholder. `GetSignatures` inspects fields, ByteRange, and applied signature contents.
- Add CMS/PKCS#7, RSA/ECDSA, certificate chains and signature validation.
- `PdfCms` builds and parses minimal CMS SignedData and signs/verifies RSA-PKCS#1 v1.5 over SHA-256 (CNG on Windows, big-number fallback elsewhere). `PdfSignatureManager::VerifySignature` recomputes the /ByteRange digest, recovers the signer key from the embedded certificate, and verifies the RSA signature.
- Add PAdES, timestamping, DSS and long-term validation foundations.
- Implement PDF/A-1 through PDF/A-4 creation and validation.
- Practical PDF/A validation: `PdfConformanceValidator` checks the file version header, forbids encryption, verifies the XMP metadata stream's `pdfaid:part`/`conformance`, requires a `/GTS_PDFA1` output intent, validates embedded TrueType/CFF fonts (plus ToUnicode for level U and tagged structure for level A), rejects transparency for PDF/A-1, and flags forbidden annotation subtypes.
- Implement PDF/UA structure trees, role maps, language and alt text.
- Add redaction and metadata/attachment sanitization.

## Phase 6: Production Quality

- Build a broad PDF, font and image compatibility corpus.
- Run differential rendering and extraction tests against MuPDF and iText outputs.
- `PdfPP.Inspect` renders pages to PPM with a machine-readable summary, and `compare_render.py` compares dimensions and dark-pixel coverage against MuPDF (`mutool` or PyMuPDF) per page; `compare_engines.py` validates page/text counts against pypdf and Poppler. `compare_text.py` compares per-page extracted text with MuPDF by token overlap, and `PdfPP.GenCorpus` writes a deterministic self-owned corpus into `tests/corpus/generated/`.
- Fuzz object parsing, streams, content, fonts and images.
- Clang libFuzzer targets (`PdfPP.FuzzReader`, `FuzzContent`, `FuzzFilter`, `FuzzCffFont`, `FuzzTrueTypeFont`) exercise object parsing, stream filters, content processing, and embedded fonts under ASan/UBSan.
- Add display-list, glyph and image caches.
- Glyph outline and advance caches now use bounded LRU storage with hit/miss counters.
- Add parallel rendering and memory budgets for large files.
- Stabilize C++, C ABI, CMake packaging and cross-platform support.
- Publish API documentation, examples, versioning and contribution policy.

## Immediate Execution Order

1. Unified display-list replay and event/state tests.
2. Form XObject resource scopes and z-order correctness.
3. Transparency groups, soft masks and blend modes.
4. Type0/CID/CFF fonts and text metrics.
5. Digital-signature foundation.
6. Practical PDF/A validation.
7. Parser and font fuzzing.