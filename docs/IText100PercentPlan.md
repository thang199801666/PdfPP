# Pdf++ — Detailed Execution Plan for 100% iText Core Parity

This document is the single source of truth for reaching **100% feature parity with
iText Core** (the open-source Java/.NET PDF library). Every row is a concrete work
item with a named acceptance test. A work item is "done" only when its acceptance
test passes in all three required configurations (default build, strict warnings-as-
errors build, ASan build) and, where noted, an external validator agrees.

Nothing in this plan requires copying iText code. All APIs and behaviors are
independent implementations of the PDF 32000-1 spec that are **behaviorally
compatible** with iText Core.

---

## 1. Acceptance methodology ("how do we know we pass 100%")

### 1.1 Feature-to-test rule

Every feature row in the iText feature matrix must map to **at least one** named
test in the `tests/` tree. The matrix (`docs/ITextFeatureMatrix.md`) gains a
`Test` column listing that test. A matrix cell is only flipped to `Implemented`
when its test passes.

### 1.2 Three build gates

Every acceptance test must pass on:

1. Default build (`build-ninja`, MSVC `/W4`).
2. Strict build (`build-strict`, `/W4 /WX`).
3. ASan build (`build-asan`, `/fsanitize=address`).

Command (all three run the same `PdfPP.UnitTests` binary):
```
ctest --test-dir <build> --output-on-failure
```

### 1.3 External validation gates

Where a row affects bytes written to disk, the output must additionally satisfy:

| Validator | Command | Checks |
|---|---|---|
| qpdf | `qpdf --check out.pdf` | xref, object structure, no warnings |
| MuPDF | `mutool info out.pdf` / `mutool draw` | structure + render sanity |
| pypdf | `python -c "import pypdf; pypdf.PdfReader('out.pdf')"` | independent parser accepts |
| iText reference | pre-generated golden files in `tests/corpus/itext/` | byte-level or value-level diff |

Golden corpus: `tests/corpus/itext/` holds PDFs produced by iText 7 (generated once,
committed, license-noted). Round-trip tests read these and assert the extracted
values match the known iText output.

### 1.4 Coverage ledger

`tools/coverage/itext_ledger.csv` (new) lists every iText capability with columns:
`module, feature, pdfpp_status (Planned/InProgress/Implemented/Verified),
test_name, external_validator, owner`. CI (or the weekly checkpoint) fails if any
row is `Implemented` without a green test.

---

## 2. Work packages (aligned to iText Core modules)

iText Core is organized as `kernel`, `io`, `layout`, `forms`, `pdfa`, `sign`, `pde`.
Pdf++ maps these to its own namespaces; the plan tracks the iText module for
comparability.

Legend for each row:
- **iText**: the iText class/feature.
- **Pdf++ today**: current status (Implemented / Partial / Missing) with file.
- **Target API**: the Pdf++ public surface to add or complete.
- **Steps**: concrete implementation steps.
- **Acceptance**: named tests (new) + existing tests that must keep passing.
- **External**: validator to run.

---

### WP-IO — `com.itextpdf.io`: byte sources, filters, image/color primitives

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `RandomAccessSourceFactory`, `ByteArrayOutputStream` | `PdfInputSource` (file/memory/stream) | File/memory/stream/custom sources already exist. Add `OpenMapped` coverage parity | 1. Keep. 2. Add tests for empty, truncated, and 0-byte sources | `IO.InputSources`, `API.InputSources` | none |
| Filter chain (`FlateDecode`, `LZWDecode`, `RunLengthDecode`, `ASCIIHexDecode`, `ASCII85Decode`, `DCTDecode`, `CCITTFaxDecode`, `JBIG2Decode`, `JPXDecode`) | Flate implemented in `PdfFilterPipeline`; others partial/missing | `PdfFilterPipeline::Decode`/`Encode` complete for all 9 PDF filters | 1. Implement LZW (TIFF prediction + early-change), RunLength, ASCIIHex/85. 2. Wrap DCT/CCITT/JBIG2/JPX behind the existing optional-codec seam (`Internal/Graphics/PdfJpegEncoder` shows the pattern). 3. Predictors: PNG up/avg/sub/paeth + TIFF | `Filters.Decoders` extended: `Filters.Lzw`, `Filters.RunLength`, `Filters.AsciiHex`, `Filters.Ascii85`, `Filters.Ccitt`, `Filters.Jbig2`, `Filters.Jpx`, `Filters.PredictorPng`, `Filters.PredictorTiff` | round-trip vs `qpdf --qdf` output on generated fixtures |
| `PdfImageXObject`, `ImageDataFactory`, `ImageType` | `PdfImage` (Png/Bmp/Jpeg/Jpx/Ccitt decode+encode, FromFile autodetect) | Match iText image-type detection and bit-depth preservation | 1. Add PNG 16-bit, palette/transparency-tRNS, interlaced decode. 2. JPEG arithmetic + CMYK. 3. Add `ImageType` enum + `DetectImageType(span)` | `API.Images`, `Feature.PngOutput`, new `Images.TypeDetection`, `Images.Png16Bit`, `Images.JpegCmyk` | `mutool info` on outputs |

**WP-IO exit:** all 9 filters have decode+encode round-trip tests green in 3 build
configs; image type detection covers PNG/JPEG/BMP/JP2/CCITT.

---

### WP-KERNEL-CORE — `com.itextpdf.kernel.pdf`: object model, xref, save modes

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `PdfObject` hierarchy | `PdfObject` (PdfNull/Bool/Int/Real/Name/String/Array/Dict/Stream/Ref) | Complete + indirect-reference copy semantics | 1. Add object copying that preserves direct/indirect identity like `PdfObject.copyTo`. 2. Add `WriteTo` streaming serializer parity | `API.ObjectModel`, `Object.Parser` | qpdf |
| `PdfDocument` lifecycle | `PdfDocument` (Open/OpenMapped/bytes/stream/source) | `Save(std::ostream)`, `GetPage`, `GetXref`, trailer introspection | 1. Add `PdfWriter::Save(std::ostream)` already exists; expose `PdfDocument::WriteTo(ostream)` | `API.DocumentOpenOverloads`, `API.WriterDocumentInfo` | qpdf |
| `PdfDocument` close / finalize | `PdfWriter::Save` | Deterministic trailer + `%%EOF`, cross-check file ID | 1. Add `/ID` first-change preservation on incremental save | `Writer.ResaveEncryptedPreservesPasswords`, `Feature.SaveValidationAndRoundTrip` | qpdf |
| Xref tables | Implemented (classic + stream) | — | 1. Keep. | `Writer.XrefStreamAndClassic`, `Reader.XrefRecovery` | qpdf |
| Xref streams | Partial (writer) | Full writer xref-stream + `/W`/`/Index` edge cases | 1. Emit `/Index` for non-contiguous xref. 2. Handle generation-number wrap | `Writer.XrefStreamAndClassic` extended | qpdf |
| Object streams | Partial (reader mature, writer off-by-default) | Writer emits `/ObjStm` with `writeObjectStreams=true` correctly encrypted | 1. Finish object-stream writer for encrypted docs (types 0/1/2 entries). 2. GC unused objects before packing | `Writer.ObjectStreamRoundTrip`, `Writer.IncrementalObjectStream`, new `Writer.ObjectStreamEncryptedGc` | qpdf |
| Incremental update | Partial (writer via `PdfIncrementalWriter`) | Cover every editor: page, annotation, AcroForm, outline, organizer, importer, highlighter | 1. Route organizer/import/highlighter edits through incremental path (currently full rewrite). 2. Add cross-session incremental (edit, save, reopen, edit again) | `Writer.IncrementalEncryptedObjectStream`, `Feature.PageMutationRemapsDependentFeatures`, new `Writer.IncrementalMultiSession` | qpdf |
| `PdfWriter` compression | Partial | Object-level `/CompressObjects`, stream reuse | 1. Add `PdfSaveOptions::compressObjects` (Flate each non-stream object). 2. Unchanged-stream passthrough (`preserveUnchangedStreams`) | `Writer.ResaveDeduplicatesStreams`, new `Writer.UnchangedStreamPassthrough` | qpdf byte-diff |
| Permissions / `PdfWriterProperties` | `PdfEncryptionOptions` | Add owner-password-empty semantics, `encryptMetadata` per spec | 1. Verify empty-owner-password case matches iText (uses random). 2. Add permissions bitmap parity tests | `Security.PasswordManagerLifecycle` extended | iText golden |

**WP-KERNEL-CORE exit:** `Writer.ObjectStreamRoundTrip` + `Writer.IncrementalMultiSession` green in 3 configs; qpdf `--check` clean on all writer outputs.

---

### WP-KERNEL-SECURITY — `com.itextpdf.kernel.pdf` security handlers

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| Standard handler R2/R3 (RC4 40/128) | RC4-128 implemented | Add RC4-40 | 1. Implement key-length 5 path in `PdfStandardSecurity` | `Security.StandardR2`, `Security.StandardR3` | iText golden |
| Standard handler R4 (AES-128) | Implemented | — | 1. Keep. | `Security.PasswordManagerLifecycle` | iText golden |
| Standard handler R6 (AES-256) | Missing | `PdfEncryptionAlgorithm::Aes256` | 1. Implement R6 key derivation (SHA-256/SHA-384/SHA-512 50-round KDF). 2. AES-256-CBC with 16-byte IV + 8-byte `Perms` entry. 3. Validate with Adobe test vectors + iText golden | `Security.Aes256R6`, `Security.Aes256Permissions`, `Security.Aes256IncrementalEdit` | iText golden, `qpdf --check` |
| Public-key security handler | Missing | `PdfPublicKeyEncryptionOptions` | 1. Envelope key with recipient X.509 via CMS. 2. Encrypt document key per recipient. 3. Decrypt path | `Security.PublicKeyEnvelope`, `Security.PublicKeyOpen` | iText golden |
| Crypt filters (`/CF`, `/StmF`, `/StrF`, identity) | Partial | Full crypt-filter dispatch | 1. Honor per-object `/CF` overrides on read and write | `Security.CryptFilterDispatch`, `Security.IdentityFilter` | qpdf |
| Metadata encryption off | Implemented | — | 1. Keep. | `Security.EncryptMetadataOff` | qpdf |

**WP-KERNEL-SECURITY exit:** AES-256 R6 round-trip + open + incremental-edit tests green in 3 configs, validated against iText-generated AES-256 golden files.

---

### WP-KERNEL-CONTENT — content streams, graphics state, color, rendering

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| Content stream parse (all operators) | `PdfContentProcessor` (most operators) | Full operator table incl. `Do`-depth, `BX`/`EX`, `sh`, `d0`/`d1` | 1. Add `BX`/`EX` compatibility sections. 2. Complete text `TJ` array semantics | `Content.Processor`, `Content.DashPatternParsing`, new `Content.CompatibilitySections` | mupdf |
| Graphics state stack | Partial | Full save/restore incl. overprint, transfer, flatness | 1. Add overprint + transfer functions to state stack | `Feature.CanvasGraphicsStateAndPaths` extended | render compare |
| Color spaces (Gray/RGB/CMYK/Indexed/Separation/DeviceN/ICCBased/Pattern) | Renderer: Gray/RGB/CMYK/Indexed/Separation/DeviceN/ICC partial; writer: RGB only | Writer `SetFillColorSpace`/`SetStrokeColorSpace` for all | 1. Add `PdfCanvas::SetColorSpace(name, csDict)` + resource registration. 2. Writer emits separation/DeviceN/ICC with tint transforms | `API.ContentCommands`, `Feature.SeparationAndDeviceNRendering`, `Feature.IccBasedRendering`, new `Writer.ColorSpaces` | render compare |
| Patterns (tiling + shading) | Tiling implemented; shading axial/radial implemented | Mesh shading (types 4/5/6/7) + function-based (type 1) | 1. Implement mesh shadings in renderer + writer. 2. Add `PdfCanvas::Shading` paint | `Feature.TilingPatternRendering`, `Feature.ShadingRenderingAndSoftMask`, new `Rendering.MeshShading` | mupdf render |
| Transparency (groups, blend modes, soft masks) | Partial (groups + some blend modes) | All 16 blend modes, isolated/knockout, soft-mask Luminosity/Alpha | 1. Complete blend mode compositing. 2. Soft mask with /Matte and both color spaces | `Content.TransparencyGroupEvents`, `Feature.TransparencyGroupRendering`, new `Rendering.SoftMaskMatte`, `Rendering.AllBlendModes` | mupdf render |
| Form XObject / tiling / Type3 glyph as XObject | Form XObject implemented | Type3 font glyph-as-form | 1. Render Type3 glyphs via their `/CharProcs` forms | `Feature.RenderingFoundation`, new `Fonts.Type3Glyph` | mupdf render |
| Text rendering modes + clipping | Implemented (0–7) | — | 1. Keep. | `Feature.CanvasTextValidation` | render compare |
| Line dash / cap / join / miter | Implemented | — | 1. Keep. | `Content.DashPatternParsing`, `Feature.DashPatternRendering` | mupdf render |

**WP-KERNEL-CONTENT exit:** `Rendering.AllBlendModes`, `Rendering.MeshShading`, `Writer.ColorSpaces`, `Fonts.Type3Glyph` green in 3 configs; differential render vs MuPDF within `compare_render.py` threshold on corpus.

---

### WP-FONTS — `com.itextpdf.kernel.font`

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `PdfType1Font` | Implemented (PFB/PFA parse + embed) | WinAnsi/MacRoman/Standard encodings | 1. Add MacRoman + Symbol/ZapfDingbats encoding tables | `Fonts.Type1Encodings`, `API.UnicodeTrueTypeWriting` | mupdf |
| `PdfTrueTypeFont` | Implemented (parse, metrics, glyph id, subset, kern, fvar) | Full cmap (format 4/6/12), `cmap` reverse, GSUB for simple ligatures | 1. Add cmap format 6 + 12. 2. Add basic GSUB (liga) for rendering | `API.UnicodeTrueTypeWriting`, `Feature.TextLayoutAndFallback`, new `Fonts.CmapFmt12`, `Fonts.GsubLiga` | mupdf render |
| `PdfType0Font` / CIDFont | Partial | CID-keyed font writer (CFF CIDFontType0C + TrueType CIDFontType2) | 1. Writer emits `DescendantFonts` + `/W` arrays. 2. Add vertical metrics (`/W2`) | `Fonts.CIDFontWriter`, `Fonts.VerticalMetrics`, `Feature.TextLayoutAndFallback` | mupdf |
| `PdfCffFont` | Implemented (Type2 charstring interpreter) | Full charstring opcode set + subrs | 1. Complete rare opcodes (hflex, flex, hintmask, cntrmask). 2. Vertical/hint handling | `Fonts.CffParser`, `Feature.EmbeddedCffFontRendering`, new `Fonts.CffFullOpcodes` | mupdf render |
| `FontProgram` / `FontProgramFactory` | Partial | `PdfFont::Load(span)` + family detection | 1. Add family/style detection (`GetFamilyName`, weight class already present) | `API.PublicArchitecture`, `Feature.TextLayoutAndFallback` | none |
| Font subsetting | TrueType implemented | CFF + Type1 subsetting | 1. Implement CFF subsetting (charset + charstrings). 2. Type1 subset with `/Widths` | `Fonts.CffSubset`, `Fonts.Type1Subset` | qpdf, mupdf |
| Fallback + shaping | Partial (fallback, basic Arabic via presentation forms) | Full shaping pipeline: bidi, Arabic, Indic, mark reordering | 1. Add HarfBuzz-style shaping (in-tree minimal shaper or optional HB dependency). 2. Mark attachment via GPOS | `Feature.TextLayoutAndFallback`, new `Text.IndicShaping`, `Text.ArabicFull` | mupdf render |

**WP-FONTS exit:** `Fonts.CIDFontWriter`, `Fonts.CffSubset`, `Fonts.GsubLiga`, `Text.ArabicFull` green in 3 configs; CJK text renders correctly vs MuPDF.

---

### WP-TEXT — extraction and search

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `PdfTextExtractor` + `ITextExtractionStrategy` | `PdfTextExtractor`, `ExtractTextChunks` | LocationTextExtraction strategy (line-by-line, columns) | 1. Implement iText-style location strategy with line/column grouping | `Text.Extractor`, new `Text.LocationStrategy` | `compare_text.py` vs MuPDF |
| `SimpleTextExtractionStrategy` | Partial | Match iText ordering | 1. Align simple strategy ordering with iText | `Text.Extractor` extended | `compare_text.py` |
| Text render mode filtering | Missing | `ExtractTextChunks(mode=Fill)` option | 1. Filter invisible (`Tr 3`) text | `Text.RenderModeFilter` | compare |
| `PdfTextSearch` | Implemented (literal + regex, geometry, index) | — | 1. Keep. | `Text.Search`, `Text.SearchOptions`, `API.TextSearch` | none |
| Tagged-text / marked-content extraction | Missing | Export marked-content structure (MCID) | 1. Parse `BDC`/`MCID` into structure spans | `Text.MarkedContentSpans`, `Validation.PdfUAStructure` | mupdf |

**WP-TEXT exit:** `Text.LocationStrategy` output matches iText strategy on golden corpus (token overlap ≥ 99%); `Text.MarkedContentSpans` green.

---

### WP-LAYOUT — `com.itextpdf.layout` (largest gap vs iText)

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `Document` (page flow, margins) | `PdfDocumentLayout::FlowParagraphs` | `PdfLayoutDocument` with margins/section breaks | 1. Refactor layout onto a layout engine with flow cursor + section breaks | `Feature.DocumentLayoutPrimitives` | render compare |
| `Paragraph` | `ParagraphOptions` | Inline runs (bold/italic/font mix), alignment, indents, keep-together | 1. Model runs; 2. justify + last-line; 3. keep-together/widows | `Layout.ParagraphRuns`, `Layout.Justify`, `Layout.KeepTogether` | render compare |
| `Table` / `Cell` | `DrawTable` (grid, fixed rows) | Full table: spanning, colspan/rowspan, borders per cell, auto column width, repeat header, page-split rows | 1. Cell model with span; 2. width algorithm; 3. header repeat + split | `Layout.TableSpan`, `Layout.TableAutoWidth`, `Layout.TablePageSplit`, `Layout.TableBorders` | render compare |
| `List` / `ListItem` | `DrawList` (bullet/decimal/alpha/roman) | Nested lists, custom markers | 1. Nested list model | `Layout.NestedLists` | render compare |
| `Tab` | Missing | `SetTabStops` | 1. Tab-stop engine with leaders | `Layout.TabStops` | render compare |
| `Image` in layout | Missing | `PdfImage::SetLayout` sizing, inline/anchored | 1. Image layout object with fit modes | `Layout.ImageInline`, `Layout.ImageFit` | render compare |
| `Div` / `AreaBreak` | Missing | Block container + page/section break | 1. Generic block box; 2. break before/after | `Layout.DivBox`, `Layout.AreaBreak` | render compare |
| `ColumnDocumentRenderer` | `DrawColumns` | Column renderer with balanced columns | 1. Column balancing algorithm | `Layout.ColumnsBalanced` | render compare |
| `HeaderFooterEventHandler` | `DrawHeader`/`DrawFooter` | Event-based page numbers + total pages | 1. `GetPageNumber()`/`GetPageCount()` fields at render time | `Layout.PageNumberFields`, `Layout.HeaderFooterEvents` | render compare |
| Default fonts + `FontProvider` | Partial | `FontSet`/`FontProvider` fallback registry | 1. Named font registry with priority fallback | `Layout.FontFallbackRegistry` | render compare |
| XHTML (iText 7 `pdfHTML` add-on) | Out of scope | Out of scope (add-on) | n/a | n/a | n/a |
| `com.itextpdf.svg` (add-on) | Out of scope | Out of scope | n/a | n/a | n/a |

**WP-LAYOUT exit:** all `Layout.*` tests green in 3 configs; generated layout PDFs render within `compare_render.py` threshold vs a parallel MuPDF render of the same content.

---

### WP-FORMS — `com.itextpdf.forms`

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `PdfAcroForm` (fields tree) | `PdfAcroForm::GetFields` | Field hierarchy + partial names | 1. Return tree with parent chain | `API.AcroFormCalculations`, new `Forms.FieldHierarchy` | qpdf |
| `PdfTextFormField` | Partial (via SetFieldValues) | Create + appearance (multi-line, password, comb, rich text) | 1. Writer creates text fields with `DA`/`DR`. 2. Multi-line/password/comb flags | `Forms.TextFieldFlags`, `Forms.CreateTextField` | mupdf |
| `PdfButtonFormField` (radio/checkbox/push) | `checked` + widget state | Radio group semantics (`/Opt` export values) | 1. Radio group with `/V`/`/AS` sync | `Forms.RadioGroup`, `Forms.CheckboxStates` | mupdf |
| `PdfChoiceFormField` | Partial | Combo + list + multi-select + editable combo | 1. `/Opt` + `/I` selected array; 2. editable combo with `/Ti` | `Forms.ChoiceCombo`, `Forms.ChoiceList`, `Forms.ChoiceMulti` | mupdf |
| Appearance generation | `GenerateAppearances` (text/numeric) | Full widget appearances incl. radio/checkbox, multi-line, font sizing | 1. Widget-specific `/AP` builders | `Forms.AppearanceRadio`, `Forms.AppearanceMultiline`, `Forms.AppearanceCombo` | render compare |
| Flattening | `FlattenFields` | Flatten removes fields + widgets, burns appearance | 1. Verify no residual `/AcroForm`. 2. Fields gone after flatten | `Forms.FlattenRemovesFields`, `Forms.FlattenPreservesVisual` | mupdf render |
| Field calculation | `CalculateFields` (arithmetic subset) | iText `PdfFormCalculations` expression subset (fields + operators) | 1. Broaden evaluator to iText's supported operators/functions | `API.AcroFormCalculations` extended | iText golden |
| XFA | Out of scope (iText pdfXfa add-on) | Out of scope | n/a | n/a | n/a |
| Annotation/widget merge | Partial | `/Annots` + `/AcroForm` widget unification | 1. Read widgets both as annotations and fields consistently | `Forms.WidgetAnnotationMerge` | qpdf |

**WP-FORMS exit:** `Forms.CreateTextField`, `Forms.RadioGroup`, `Forms.ChoiceCombo`, `Forms.FlattenRemovesFields` green in 3 configs; flattened output renders identical to pre-flatten (pixel diff 0).

---

### WP-ANNOT — annotations, outlines, destinations, structure

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| Annotation types (all standard) | 12 types implemented | Line, Screen, Movie, Sound, FileAttachment, Widget, Caret | 1. Add Line (with leader lines), FileAttachment, Screen | `API.AdvancedAnnotationsAndXfdf`, new `Annotations.Line`, `Annotations.FileAttachment` | mupdf |
| Appearance streams | `GenerateAppearances` (native-drawable) | Every type has deterministic `/AP /N` | 1. Extend to new types | `Annotations.AppearanceAllTypes` | mupdf render |
| `PdfOutline` (bookmarks) | Implemented (hierarchical) | Destinations + open/close state | 1. Keep; verify `isOpen` round-trip | `API.WriterBookmarks`, `Feature.BookmarkValidationAndLifecycle` | qpdf |
| Named destinations + actions | `Feature.NamedDestinationsAndLinks` | Full action dicts: GoTo/GoToR/URI/Launch/Named | 1. Add GoToR + Launch + `NA` (next action) | `Feature.NamedDestinationsAndLinks` extended, new `Actions.GoToRemote`, `Actions.Launch` | qpdf |
| Page labels | Implemented | — | 1. Keep. | `Feature.PageLabelsLifecycleAndRemapping` | qpdf |
| Optional content (OCG) | Implemented | — | 1. Keep; add visibility-policy edit | `Feature.OptionalContentLayers` | qpdf |
| Tagged PDF / structure tree | `SetTaggedPdf` (basic) | Full `StructTreeRoot`, role map, MCID, alt text, `PDF/UA` | 1. Struct elements per page; 2. `/Lang` + `/Alt` for images; 3. roles mapping table | `Validation.PdfUAStructure`, new `Tagged.StructureTree`, `Tagged.AltText`, `Tagged.RoleMap` | qpdf, mupdf |
| XFDF | Implemented (round-trip) | Full schema for new annotation types | 1. Extend to Line/FileAttachment/Screen | `API.AdvancedAnnotationsAndXfdf` extended | none |

**WP-ANNOT exit:** `Annotations.Line`, `Actions.GoToRemote`, `Tagged.StructureTree`, `Tagged.AltText` green in 3 configs.

---

### WP-SIGN — `com.itextpdf.signatures`

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `PdfSigner` (sign placement) | `PdfSignatureManager::Sign` (external digest) | In-process signing with provided key | 1. Accept RSA/ECDSA key material and produce PKCS#7 | `Security.CryptoPrimitivesAndAlgorithms`, new `Sign.InProcess` | mupdf |
| CMS / PKCS#7 | `PdfCms` (build + parse minimal) | Full CMS SignedData: signed attrs, certificates, CRLs, digest attrs | 1. Add `signedAttrs` (`content-type`, `message-digest`, `signing-time`). 2. Add CRL/cert bundles | `Security.CryptoPrimitivesAndAlgorithms` extended, new `Sign.SignedAttrs`, `Sign.CrlBundle` | OpenSSL verify |
| RSA / ECDSA | Implemented (RSA, ECDSA P-256) | P-384/P-521 + Ed25519 | 1. Add curves | `Security.CryptoPrimitivesAndAlgorithms` extended | OpenSSL |
| Certificate chain validation | `ValidateCertificate` (basic) | Full chain build to trust anchor | 1. Chain building + revocation check hooks | `Security.CertificateChainValidation` | OpenSSL |
| `PdfPKCS7` verify | `VerifySignature` (recompute ByteRange digest + RSA verify) | Verify signed attrs + chain | 1. Verify `signedAttrs` digest | `Security.CryptoPrimitivesAndAlgorithms` extended | OpenSSL |
| `PdfSigner` appearance | Basic | Signature appearance with name/date/logo | 1. Build `/AP` with text + optional image | `Sign.Appearance` | render compare |
| `LtvVerifier` / DSS | `PdfDss::AddDocumentSecurityStore` (DSS + VRI) | Full LTV: OCSP/CRL timestamps, validation by revocation info | 1. RFC 3161 timestamp requests. 2. VRI per-signature update | `Standards.PadesLtv`, `Security.ValidationInfo` | OpenSSL, mupdf |
| PAdES baseline (B/BES/EPES/LTA) | Partial | PAdES-B/EPES + LTA | 1. B: basic CMS. 2. BES: signed attrs. 3. EPES: policy. 4. LTA: DSS+timestamp | `Standards.PadesB`, `Standards.PadesBES`, `Standards.PadesEpes`, `Standards.PadesLta` | mupdf |

**WP-SIGN exit:** `Sign.InProcess` and `Standards.PadesB` green in 3 configs; signature verifies under OpenSSL `openssl cms -verify`.

---

### WP-PDFA — `com.itextpdf.pdfa`

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| PDF/A-1b/1a creation | Validation only | `PdfConformance::ConfigureForPdfA(level)` on writer | 1. Output intent `/GTS_PDFA1`. 2. XMP `pdfaid` metadata. 3. Font embedding enforcement. 4. Color space restrictions. 5. No transparency (1a also tags) | `Standards.PdfACreation1`, `Standards.PdfACreation1a` | `qpdf --check`, veraPDF |
| PDF/A-2b/2a creation | Missing | Same + object streams OK, extended color | 1. Version 1.7 header; 2. allow transparency + ICC | `Standards.PdfACreation2` | veraPDF |
| PDF/A-3b/3a + attachments | Missing | PDF/A-3 embeds arbitrary files | 1. `/AF` relationship + file specification | `Standards.PdfACreation3` | veraPDF |
| PDF/A-4 (PDF 2.0) | Missing | PDF 2.0 creation path | 1. Version 2.0 + new output intent | `Standards.PdfACreation4` | veraPDF |
| PDF/A validation parity | `PdfConformanceValidator` | Match veraPDF pass/fail on corpus | 1. Test each validation rule against veraPDF results | `Validation.PdfAConforming`, `Validation.PdfAMissingMetadataAndOutput`, `Validation.PdfAPartMismatch` | veraPDF |

**WP-PDFA exit:** `Standards.PdfACreation1/2/3/4` green in 3 configs AND output validates with veraPDF CLI (installed in CI) with zero errors.

---

### WP-KERNEL-IMAGE — images, codecs, masks

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| ICC profile handling | Partial (N-component identity transform) | Full ICC (LUT + matrix) via optional lcms2 | 1. Add optional `lcms2` codec seam; 2. fallback identity when absent | `Feature.IccBasedRendering`, `Feature.IccSrgbGammaRendering`, new `Images.IccMatrixTransform` | mupdf render |
| Soft masks | Implemented (size-independent sampling) | `/Matte` + both soft-mask color spaces | 1. Add /Matte undo; 2. Luminosity via gray conversion | `Feature.ShadingRenderingAndSoftMask`, new `Rendering.SoftMaskMatte` | mupdf render |
| Indexed + masks | Implemented | — | 1. Keep. | `Feature.SeparationAndDeviceNRendering`, `Reader.SoftMaskImageExtraction` | mupdf |
| Inline images | Implemented (read) | Writer emits inline images | 1. `PdfCanvas::InlineImage` | `Reader.InlineImageExtraction`, new `Writer.InlineImage` | qpdf |
| Image cache | Decoded-image per-pass cache | Inter-pass cache budget | 1. Add bounded cross-page cache | `Reader.ObjectCacheAndLimits` extended | perf |

**WP-KERNEL-IMAGE exit:** `Images.IccMatrixTransform`, `Rendering.SoftMaskMatte`, `Writer.InlineImage` green in 3 configs.

---

### WP-CMAKE-API — packaging, ABI, build parity

| iText | Pdf++ today | Target API | Steps | Acceptance | External |
|---|---|---|---|---|---|
| `itext7` maven artifacts | CMake install + package config | `CPPPdf::Core` find_package works for consumers | 1. Test a downstream `find_package` consumer (see `examples/`). 2. Pkg-config file | `API.PublicArchitecture`, CI job `package-consumer` | cmake |
| C ABI | `pdfpp_c.h` | Stable symbols + versioning | 1. Add `pdfpp_c_version()`; 2. ABI check script | `API.CApi` extended, `tools/abi/check.py` | none |
| Versioning policy | `Version.hpp` | ABI version 1 | 1. Keep `Version.hpp`; set SOVERSION 1 | `API.PublicArchitecture` | none |
| Cross-compiler | MSVC ok; GCC/Clang required | CI matrix GCC+Clang+MSVC, `/WX`/`-Werror` | 1. Add CI matrix; 2. Fix Clang/GCC warnings to zero | CI green on 3 compilers | none |

**WP-CMAKE-API exit:** downstream consumer builds against installed package on MSVC + GCC + Clang; C ABI version check green.

---

## 3. Milestones and dependency graph

```
M1  WP-IO -> WP-KERNEL-CORE          (storage + filters)
     | 
     +-> WP-KERNEL-SECURITY          (encryption parity)     [depends M1 storage]
M2  WP-FONTS -> WP-KERNEL-CONTENT    (fonts + graphics)
     +-> WP-TEXT
M3  WP-LAYOUT                        (layout engine)          [depends M2 fonts]
M4  WP-FORMS -> WP-ANNOT             (forms + annotations)    [depends M2]
M5  WP-SIGN -> WP-PDFA               (signatures + pdfa)      [depends M1 security]
M6  WP-KERNEL-IMAGE -> WP-CMAKE-API  (images + packaging)     [depends M2]
```

### Milestone exit checklists

- **M1**: WP-IO + WP-KERNEL-CORE + WP-KERNEL-SECURITY. Exit: `Writer.ObjectStreamEncryptedGc`, `Security.Aes256R6`, `Writer.IncrementalMultiSession` green in 3 configs; qpdf clean.
- **M2**: WP-FONTS + WP-KERNEL-CONTENT + WP-TEXT. Exit: `Fonts.CIDFontWriter`, `Rendering.AllBlendModes`, `Text.LocationStrategy` green; CJK/Arabic render vs MuPDF within threshold.
- **M3**: WP-LAYOUT. Exit: `Layout.TableSpan`, `Layout.TablePageSplit`, `Layout.PageNumberFields` green; layout renders match MuPDF on corpus.
- **M4**: WP-FORMS + WP-ANNOT. Exit: `Forms.RadioGroup`, `Forms.FlattenRemovesFields`, `Annotations.Line`, `Tagged.StructureTree` green.
- **M5**: WP-SIGN + WP-PDFA. Exit: `Standards.PadesB`, `Standards.PdfACreation1` green; OpenSSL verify + veraPDF clean.
- **M6**: WP-KERNEL-IMAGE + WP-CMAKE-API. Exit: `Images.IccMatrixTransform`, downstream consumer builds on 3 compilers, ABI check green.

---

## 4. Weekly checkpoint protocol

1. Run the three build gates; record pass/fail in `docs/status.md`.
2. Run `tools/coverage/itext_ledger.csv` diff; every `Implemented` row must reference a green test.
3. Run external validators on the union of generated fixtures:
   `qpdf --check`, `mutool info`, `pypdf` open, `veraPDF` (M5+).
4. Update `docs/ITextFeatureMatrix.md` status column; append `CHANGELOG.md` entry.
5. Re-baseline `docs/Benchmarks-*` if perf-affecting changes merged.

---

## 5. Definition of "100% pass"

The project is at 100% iText Core parity when:

1. Every row of `docs/ITextFeatureMatrix.md` reads `Implemented` (no `Partial`/`Missing`
   within scope; HTML/SVG/OCR/XFA excluded as add-ons and marked `N/A`).
2. Every `Implemented` row has its named acceptance test green in the 3 build configs.
3. Every external-validator cell (`qpdf`, `mutool`, `pypdf`, `veraPDF`, `OpenSSL`) passes
   on the corresponding fixtures.
4. `tools/coverage/itext_ledger.csv` has zero `Implemented-without-test` rows.
5. The C ABI + CMake packaging tests pass, and the downstream consumer builds on
   MSVC, GCC, and Clang with zero warnings.
