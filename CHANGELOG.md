## 0.116.0

- UTF-8 helpers: `PdfTextLayout::CountCodePoints` and `TruncateUtf8` (with an
  ellipsis, cutting at grapheme boundaries so combining marks stay attached).
- Tests: `API.TextSearch` verifies counts and truncation of `he` + combining
  acute. All suites green.

## 0.115.0

- Writer page rotation: `PdfWriter::SetPageRotation`/`GetPageRotation` emit
  the page `/Rotate` value (0/90/180/270).
- Tests: `API.WriterDocumentInfo` rotates a page, saves, and reads it back. All
  suites green.

## 0.114.0

- Region text extraction: `PdfDocument::GetPageTextInRegion` returns text
  inside a page rectangle. Fixed text-line origin reset at `BT` so multiple
  text objects in one page position correctly.
- Tests: `Feature.RegionTextExtraction` extracts per-half regions of a two-line
  page. All suites green.

## 0.113.0

- GPOS SinglePos: the TrueType parser reads lookup-type-1 per-glyph xAdvance
  adjustments (formats 1 and 2); `GetGlyphAdvanceAdjustment` exposes them and
  the renderer includes them in advance totals.
- Tests: `Feature.TextLayoutAndFallback` checks an adjustment for glyph A. All
  suites green.

## 0.112.0

- Text state operators: `PdfCanvas` gains `SetTextLeading`, `SetTextRise`,
  `SetHorizontalScaling`, `SetCharSpacing`, and `SetWordSpacing`.
- Tests: `Feature.TextStateOperators` writes and renders text using all five
  operators. All suites green.

## 0.111.0

- GPOS PairPosFormat2 (class-based kerning): the TrueType parser now reads
  class-definition subtables and applies xAdvance pairs between glyph classes,
  covering fonts whose kern data lives in class records rather than explicit
  pairs (e.g. Arial).
- Tests: `Feature.TextLayoutAndFallback` still finds a non-zero "AV" kern. All
  suites green.

## 0.110.0

- Renderer applies GPOS mark-to-mark positioning for stacked diacritics: a
  mark over another mark is nudged onto the mark anchor (falls back to
  mark-to-base anchors).
- Tests: `Feature.TextLayoutAndFallback` exercises the positioning path. All
  suites green.

## 0.109.0

- GPOS MarkMarkPos: the TrueType parser reads lookup-type-6 mark-to-mark
  anchor attachments (format 1) for stacked diacritics; `GetMarkMarkPosition`
  returns the anchor pair.
- Tests: `Feature.TextLayoutAndFallback` verifies the mark-to-mark store. All
  suites green.

## 0.108.0

- PNG decoding: `PdfImage::FromPng` reads PNG files (RGB/RGBA/palette/gray,
  bit depths 1-16, non-interlaced) via zlib inflate + scanline filters and
  returns a raw RGB image with alpha composited over black.
- Tests: `Feature.PngOutput` round-trips a rendered PNG back into a PdfImage.
  All suites green.

## 0.107.0

- Tiling patterns (write): `PdfWriter::AddTilingPattern` registers a pattern
  and `PdfCanvas::SetPattern` paints subsequent fills/strokes with it. The
  writer emits `/Pattern` resources and `/PatternType 1` streams.
- Tests: `Feature.TilingPatternWrite` writes a hatch pattern, checks the
  output, and renders the page. All suites green.

## 0.106.0

- CFF advance widths: `PdfCffParser::GetAdvanceWidth` returns a glyph's width
  in font units (charstring width operand or the private dict default).
- Tests: `Feature.CffFontEmbedding` resolves a width for the minimal font. All
  suites green.

## 0.105.0

- Page resizing: `PdfWriter::SetPageSize` changes an existing page's media box
  after creation.
- Tests: `API.WriterDocumentInfo` resizes a page, saves, and reads the new box
  back. All suites green.

## 0.104.0

- Bitmap mirroring: `PdfBitmap::FlipHorizontal` and `FlipVertical` return
  mirrored copies.
- Tests: `Feature.PngOutput` checks corner pixels after both flips. All
  suites green.

## 0.103.0

- XMP metadata: `PdfWriter::SetXmpMetadata` embeds an XMP packet (built from
  the document info) as the catalog `/Metadata` stream, required for PDF/A.
- Tests: `API.WriterDocumentInfo` verifies the `/Metadata` object and packet
  contents. All suites green.

## 0.102.0

- Content inspection: `PdfDocument::GetPageContentStream` returns a page's
  decoded content stream (all /Contents concatenated).
- Tests: `Feature.PageContentStream` verifies written BT/Tj operators and
  out-of-range rejection. All suites green.

## 0.101.0

- Regex redaction: `PdfRedactor::RedactRegex` covers every text occurrence
  matching a regular expression (case-insensitive by default) per page.
- Tests: `Feature.Redaction` redacts two phone numbers via one pattern. All
  suites green.

## 0.100.0

- Page label reading: `PdfDocument::GetPageLabels` parses the catalog
  `/PageLabels /Nums` array and returns sorted ranges with style, prefix, and
  start number.
- Tests: `Writer.ObjectStreamAndPageLabels` round-trips writer page labels
  through the reader. All suites green.

## 0.99.0

- Bitmap manipulation: `PdfBitmap` gains bilinear `Resize`, `Crop`, and
  `Rotate90` (multiples of 90 degrees) for thumbnails and orientation fixes.
- Tests: `Feature.PngOutput` verifies resized aspect ratio, cropped
  dimensions, and rotated shape. All suites green.

## 0.98.0

- Word extraction: `PdfTextExtractor::ExtractWords` groups text chunks into
  words (splitting on whitespace and horizontal gaps) and returns each word's
  bounding box, useful for text selection.
- Tests: `API.TextSearch` verifies "Hello World" yields two words with correct
  boxes. All suites green.

## 0.97.0

- Renderer GPOS mark positioning: combining marks whose anchor is defined over
  the previous base glyph are nudged onto the base anchor during rendering, so
  diacritics sit correctly instead of following the baseline.
- Tests: `Feature.TextLayoutAndFallback` renders `e` + combining acute through
  the renderer. All suites green.

## 0.96.0

- JPEG output: `PdfBitmap::SaveJpeg` writes a baseline JPEG (quality 1-100)
  using the internal encoder with no external libraries.
- Tests: `Feature.PngOutput` additionally writes a JPEG and checks the SOI
  marker. All suites green.

## 0.95.0

- Document conveniences: `PdfDocument::GetPageMediaBox` and `GetAllPagesText`
  (all pages' extracted text joined with newlines).
- Tests: `API.PageEditingAndOrganization` exercises both. All suites green.

## 0.94.0

- Keyword search without modification: `PdfKeywordHighlighter::FindMatches`
  locates every occurrence of a keyword and returns page indices and bounding
  rectangles without writing a highlighted copy.
- Tests: `API.AnnotationsAndHighlight` asserts the search finds the same two
  matches as highlighting, without touching the input. All suites green.

## 0.93.0

- PNG output: `PdfBitmap::SavePng` writes a true-color RGBA PNG (IHDR/IDAT/IEND,
  zlib-compressed, correct CRC) with no external libraries.
- Tests: `Feature.PngOutput` renders a colored page to PNG and verifies the
  signature and chunk structure. All suites green.

## 0.92.0

- Page rotation: `PdfPageEditor::SetPageRotation` sets a page's `/Rotate`
  value (multiple of 90) via an incremental update.
- Tests: `API.PageEditingAndOrganization` rotates a page to 90 degrees, reads
  it back, and rejects non-multiples of 90. All suites green.

## 0.91.0

- Ellipse/circle drawing: `PdfCanvas` gains `DrawEllipse`, `FillEllipse`,
  `DrawCircle`, and `FillCircle`, built from two cubic bezier segments
  (k = 0.5523).
- Tests: `Feature.PolygonAndBezierPaths` adds a stroked circle and filled
  ellipse and verifies rendering. All suites green.

## 0.90.0

- Path construction: `PdfCanvas` gains `SetLineDash`, `DrawPolyline`,
  `DrawPolygon`, `FillPolygon`, and cubic `DrawBezier`/`FillBezier` helpers.
- Tests: `Feature.PolygonAndBezierPaths` writes a dashed stroked triangle and
  bezier curve and verifies the page renders cleanly. All suites green.

## 0.89.0

- ICC color management: the renderer detects standard sRGB ICC profiles on
  ICCBased RGB images and applies the profile's transfer curve (gamma) so
  images render with correct tone instead of a raw pass-through.
- Tests: `Feature.IccSrgbGammaRendering` builds a minimal sRGB profile (rTRC
  'curv' gamma 2.2) and verifies a mid-tone sample is re-encoded (~210 vs 128).
  All suites green.

## 0.88.0

- Convenience search: `PdfDocument::SearchText` runs the shared literal search
  over a page's extracted chunks and returns matches with bounding boxes.
- Tests: `API.TextSearch` round-trips a written page and verifies a
  case-insensitive hit plus out-of-range rejection. All suites green.

## 0.87.0

- GPOS MarkBasePos: the TrueType parser reads lookup-type-4 mark-to-base anchor
  attachments (format 1) and `GetMarkBasePosition` returns the anchor pair for
  a combining-mark/base-glyph pair, enabling positioned combining diacritics.
- Tests: `Feature.TextLayoutAndFallback` verifies Arial's GPOS mark anchors for
  `e` + combining acute. All suites green.

## 0.86.0

- Outline reading: `PdfDocument::GetOutlines` walks the `/Outlines` tree and
  returns flat `PdfOutlineEntry` bookmarks (title, destination page, depth)
  with cycle protection.
- Tests: `Writer.ObjectStreamAndPageLabels` round-trips an `AddBookmark` outline
  and verifies title/destination through `GetOutlines`. All suites green.

## 0.85.0

- GPOS PairPos kerning: the TrueType parser now reads OpenType `GPOS`
  lookup-type-2 PairPosFormat1 subtables (xAdvance only) and merges the pairs
  into the kern store, covering modern fonts that omit the legacy `kern` table.
- Tests: `Feature.TextLayoutAndFallback` asserts a non-zero "AV" kern from the
  system font (Arial/DejaVu place it in GPOS). All suites green.

## 0.84.0

- Indexed palette optimization: the writer converts small RGB images (<=32
  unique colors) to an `/Indexed /DeviceRGB` color space with a compressed
  index plane, shrinking palette-friendly images.
- Tests: `Feature.JpxImageWrite` writes a 2-color image and verifies `/Indexed`
  output plus image round-trip. All suites green.

## 0.83.0

- Page geometry editing: `PdfPageEditor::SetPageBox` sets a page's `/CropBox`
  or `/MediaBox` via an incremental update.
- Tests: `API.PageEditingAndOrganization` crops a page and reads back the box,
  and checks out-of-range rejection. All suites green.

## 0.82.0

- Renderer kerning: `DrawTextChunk` applies the embedded font's cached kern
  pairs between consecutive glyphs, so TrueType text renders with proper
  horizontal adjustments.
- Tests: `Feature.TextLayoutAndFallback` renders a kerning-enabled font through
  `PdfPageRenderer`. All suites green.

## 0.81.0

- Embedded CFF (Type1C) font writing: `PdfCanvas::SetEmbeddedCffFontAndSize`
  selects a parsed CFF font and the writer emits a `/Subtype /Type1` font with
  a `/FontFile3 /Subtype /Type1C` stream and `/FontDescriptor`.
- Tests: `Feature.CffFontEmbedding` embeds a hand-built CFF font and verifies
  the `/FontFile3`/`/Type1C` output. All suites green (33 feature subtests).

## 0.80.0

- Tagged PDF: `PdfWriter::SetTaggedPdf`/`SetLanguage` write `/MarkInfo
  /Marked true`, `/Lang`, and a minimal `/StructTreeRoot` (with `/ParentTree`
  and a document `/RoleMap`) to the catalog, producing PDF/UA-ready output.
- Tests: `Feature.TaggedPdf` verifies the catalog carries `/MarkInfo`, `/Lang`,
  and `/StructTreeRoot`. All suites green (32 feature subtests).

## 0.79.0

- OpenType GSUB ligature substitution: `PdfTrueTypeFont` parses the GSUB
  LigatureSubst lookups (type 4) and `ApplyLigatures` replaces matched component
  glyph sequences with the ligature glyph (e.g. fi -> fi ligature).
- Tests: `Feature.TextLayoutAndFallback` substitutes an f+i pair when the font
  has GSUB ligatures. All suites green.

## 0.78.0

- Page duplication: `PdfPageOrganizer::DuplicatePages` appends copies of the
  requested pages at the end of the document via the page importer, validating
  indices.
- Tests: `API.PageEditingAndOrganization` duplicates page 0 (2 -> 3 pages) and
  checks out-of-range rejection. All suites green.

## 0.77.0

- CCITT Group 3/4 one-dimensional decoding: `PdfImage::DecodeCcittG4` converts a
  fax codestream back to packed 1-bit rows using the run-length terminator
  codes and EOL resynchronization.
- Tests: `Feature.JpxImageWrite` also decodes the encoded G4 payload. All
  suites green.

## 0.76.0

- C ABI: added `pdfpp_c.h`/`pdfpp_c.cpp` exposing the core through opaque
  handles — `pdfpp_open`, `pdfpp_page_count`, `pdfpp_page_text`,
  `pdfpp_render_ppm`, `pdfpp_version`, and `pdfpp_close` — so the library can
  be consumed from C, FFI, or scripting runtimes.
- Tests: `API.CApi` opens a file, reads text, renders to PPM, and checks
  null-handle error handling. All suites green.

## 0.75.0

- JPEG encoder: `PdfImage::EncodeJpeg` writes a baseline JPEG (DCT, 4:4:4,
  quality 1-100) from RGB data without external libraries, including SOI/APP0/
  SOF0/DQT markers, YCbCr conversion, forward DCT, zigzag reorder, and Huffman
  bit coding.
- Tests: `Feature.JpxImageWrite` also round-trips an encoded JPEG through
  `FromJpeg` (dimensions + DCT encoding). All suites green.

## 0.74.0

- PDF/UA validation checks: `PdfConformanceValidator` now also requires a
  `/Lang` catalog entry (`PDFUA-LANG-001`) and `/MarkInfo /Marked true`
  (`PDFUA-MARKED-001`) in addition to the structure tree root.
- Tests: `Validation.PdfUAStructure` verifies the new issues are reported.
  All suites green.

## 0.73.0

- CCITT Group 4 fax encoding: `PdfImage::EncodeCcittG4` produces a CCITT
  codestream from 1-bit image data using run-length terminator codes (rows
  encoded in horizontal mode with EOL markers).
- Tests: `Feature.JpxImageWrite` also verifies a non-empty G4 payload. All
  suites green.

## 0.72.0

- Certificate validation: `PdfCms::ValidateCertificate` checks the validity
  window against a reference time and walks a basic issuer chain; self-signed
  leaves are reported as `SelfSigned`, out-of-window certificates as `Expired`/
  `NotYetValid`, and malformed inputs as `Malformed`.
- `CertificateInfoOf` now decodes UTCTime/GeneralizedTime to real Unix seconds
  (civil-date conversion) instead of a year-scaled approximation.
- Tests: `verifyCertificateInfo` checks Valid-at-2026/Expired-at-2040 statuses.
  All suites green.

## 0.71.0

- Type1 font embedding: `PdfType1Font` parses PFB (and PFA) programs for the
  font name and standard 256-character widths, and `PdfCanvas::SetType1FontAndSize`/
  `ShowType1Text` write an embedded `/Subtype /Type1` font with a `/FontFile`
  stream, `/WinAnsiEncoding`, and a `/Widths` array.
- Tests: `Feature.Type1FontEmbedding` builds a minimal PFB, embeds it, and
  verifies the `/Type1`, `/FontFile`, and `/WinAnsiEncoding` output. All suites
  green (31 feature subtests).

## 0.70.0

- Fixed `PdfCms::CertificateInfoOf`: the tbsCertificate walker read the
  signature algorithm twice, shifting the issuer/validity/subject offsets. The
  certificate now reports subject/issuer common names, the validity window, and
  the self-signed flag correctly.
- Tests: `verifyCertificateInfo` checks subject, issuer, self-signed, validity,
  and notAfter > notBefore for a real self-signed certificate.

## 0.69.0

- PAdES foundations: `PdfDss::AddDocumentSecurityStore` writes a catalog `/DSS`
  entry with embedded `/Certs`, `/CRLs`, `/OCSPs`, and `/VRI` timestamp data via
  an incremental update; `HasDocumentSecurityStore` detects an existing DSS.
- Tests: `verifyDss` adds a DSS with a certificate and verifies the catalog
  `/DSS` round trip. All suites green.

## 0.68.0

- Basic Arabic shaping: `PdfTextLayout::ShapeArabic` joins Arabic letters into
  their contextual presentation forms (isolated/initial/medial/final) using the
  standard Arabic presentation forms table, enabling correct rendering of
  connected Arabic text.
- Tests: `Feature.TextLayoutAndFallback` verifies ب + ت join to their initial/
  final forms. All suites green.

## 0.67.0

- Image encoding on write: the writer now keeps JPEG (DCT), JPEG 2000 (JPX)
  and CCITT fax sources in their native encoding (`/DCTDecode`, `/JPXDecode`,
  `/CCITTFaxDecode`) instead of re-compressing with Flate, and a
  `PdfSaveOptions::preserveImageEncodings` flag controls the behavior.
- `PdfImage::FromJpeg2000` parses the JPX SIZ marker for width/height and
  `PdfImage::FromCcitt` wraps CCITT Group 4 payloads.
- Tests: `Feature.JpxImageWrite` writes a minimal JPEG 2000 image and verifies
  the `/JPXDecode` filter and extraction. All suites green (30 feature
  subtests).

## 0.66.0

- Vertical writing: `PdfCanvas::SetVerticalWriting` rotates the text matrix 90°
  for top-to-bottom runs, `IsVerticalWriting` reports the state, and
  `ShowTextVertical` draws a single vertical run. Useful for CJK text.
- Tests: `Feature.TextLayoutAndFallback` covers vertical-mode toggling and a
  vertical text run. All suites green.

## 0.65.0

- Parallel rendering: `PdfPageRenderer::RenderAllPagesParallel` renders every
  page concurrently, opening an independent `PdfDocument` per worker to avoid
  shared mutable cache state; falls back to sequential rendering for single
  pages and rethrows the first worker error.
- Added `examples/report.cpp` demonstrating headers/footers, lists, columns,
  portfolios, and parallel rendering, plus a README "Examples" section.
- Memory budgets were already available via `PdfReaderOptions::limits`
  (object/stream/page/cache caps and memory-map threshold); CMake configuration
  is verified with tests + validation tools enabled.
- Tests: `Feature.ParallelRendering` checks parallel output matches sequential
  rendering pixel-for-pixel. All suites green (29 feature subtests).

## 0.64.0

- ECDSA (NIST P-256) signature support in `PdfCms`: `EcDsaSign`/`EcDsaVerify`
  sign and verify SHA-256 digests via CNG on Windows (DER ECDSA-Sig-Value).
- PDF redaction: `PdfRedactor::RedactText` finds literal text on a page and
  covers it with opaque black rectangles via an appended content stream.
- Certificate introspection: `PdfCms::CertificateInfoOf` extracts subject/
  issuer, validity window, and self-signed flag from a DER X.509 certificate
  (exhaustive name/chain validation is future PAdES work).
- Tests: `verifyEcdsa` (sign/verify + wrong-digest rejection) and
  `Feature.Redaction` cover the new features. All suites green.

## 0.63.0

- PDF portfolios: `PdfWriter::SetPortfolio`/`ClearPortfolio`/`HasPortfolio`
  mark the document as a collection with a catalog `/Collection` entry (view
  mode + optional title), grouping embedded files into a browsable shell.
- Tests: `Feature.Portfolio` verifies the `/Collection` catalog entry, view
  mode, and title round trip. All suites green (27 feature subtests).

## 0.62.0

- AcroForm field calculations: `PdfAcroForm::CalculateFields` reads `/AA /C`
  JavaScript calculation scripts and evaluates the common `+ - * /`
  arithmetic (with parentheses, literals, and field-name references) via a
  restricted evaluator, then writes the recalculated `/V` values.
- `GetFields` now reports numeric (integer/real) field values correctly via
  `objectTextValue` (previously only strings and names were read).
- Added `PdfFormActionType`/`PdfFormAction` placeholders for field actions.
- Tests: `API.AcroFormCalculations` verifies `total = A + B` recalculates to
  15. All suites green.

## 0.61.0

- TrueType kerning: the `kern` table is parsed and `PdfTrueTypeFont` exposes
  `GetKerning`/`GetCachedKerning` (font units scaled to size) with an LRU cache.
- Font fallback: `PdfCanvas::ShowTextUtf8WithFallback` splits text into runs so
  each code point uses the first font (primary or a fallback) that covers it,
  applying per-run kerning via the `TJ` adjustment array.
- Added `PdfCanvas::GetCurrentFontSize` and page-level font-size tracking.
- Added `PdfTextLayout`: UAX #29 grapheme clustering (base + combining marks)
  and simplified UAX #9 bidirectional reordering for LTR/RTL paragraphs.
- Added `PdfDocumentLayout`: paragraph flow with automatic page breaks, bullet/
  decimal/alpha/roman lists, multi-column text, and page headers/footers with
  page numbers.
- Tests: `Feature.TextLayoutAndFallback` and `Feature.DocumentLayoutPrimitives`
  cover kerning, fallback, bidi/grapheme, lists, columns, headers and footers.
  All suites green (26 feature subtests).

## 0.60.0

- Added CMS/PKCS#7 and RSA-PKCS#1 v1.5 signature support (`PdfCms`):
  - DER reader/writer for minimal CMS SignedData (single signer, detached).
  - RSA PKCS#1 v1.5 sign/verify over SHA-256. On Windows this uses CNG
    (bcrypt) with a self-contained big-number fallback for other platforms.
  - PEM private-key and DER certificate parsing, plus RSA public-key recovery
    from an embedded X.509 certificate.
- `PdfSignatureManager::VerifySignature` recomputes the SHA-256 digest over the
  signature /ByteRange regions, extracts the signer certificate and signature
  from the CMS /Contents value, and verifies the RSA signature. Statuses:
  `Valid`, `InvalidSignature`, `DigestMismatch`, `Malformed`, `NotApplicable`.
- Tests: `verifySignatureVerification` exercises the end-to-end VerifySignature
  flow. All suites green.

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
