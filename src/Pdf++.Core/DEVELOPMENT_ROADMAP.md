# Pdf++ — Current Status and Next Development Roadmap

## 1. Project policy

- Core language: **C++20**.
- The core will **not** be rewritten in pure C.
- A stable C ABI may continue to be expanded for bindings to C, C#, Python, Rust, Go, and Java.
- Public version remains fixed at **1.0.0** during the current development and hardening cycle.
- Delivery format remains a cumulative ZIP containing only new or modified files, with the original directory structure preserved.

## 2. Current baseline

This roadmap starts from:

- Package: `Pdf++.Core_iText_gap_batch9_conformance_security_cumulative_patch.zip`
- Size: **326,057 bytes**
- SHA-256: `7951b462e9f687ffda83ef9b7f791d9dc198a90f7d6c6c997026112d87c6801e`
- Test status:
  - Release: **20/20 passed**
  - GCC strict with `-Werror`: **20/20 passed**
  - Clang strict with `-Werror`: **20/20 passed**
  - ASan + UBSan: **20/20 passed**
  - Poppler AES-256 R6 validation: passed
  - OpenSSL CMS verification: passed
  - Poppler graphics, JPEG, Type3, and mesh rendering: passed

## 3. Major capabilities already implemented

### Security and incremental editing

- AES-256 Revision 6.
- Revision 6 password derivation and validation.
- `/O`, `/U`, `/OE`, `/UE`, and `/Perms`.
- AES-256 object encryption.
- Operating-system CSPRNG.
- Incremental editing of encrypted documents.
- Public incremental update engine.
- Classic xref and xref-stream revisions.
- Multi-session revision chains.

### CMS, signatures, and PAdES foundation

- CMS `SignedData`.
- Signed attributes:
  - Content type.
  - Message digest.
  - Signing time.
- Certificate-chain embedding.
- OpenSSL-compatible CMS serialization.
- Signature placeholder and ByteRange foundation.
- DSS and VRI.
- Multi-session DSS merge.
- Signature-content SHA-1 VRI keys.
- Certificate, CRL, OCSP, and timestamp containers.

### PDF/A

Creation support currently includes:

- PDF/A-1A and PDF/A-1B.
- PDF/A-2A, PDF/A-2B, and PDF/A-2U.
- PDF/A-3A, PDF/A-3B, and PDF/A-3U.
- PDF/A-4, PDF/A-4E, and PDF/A-4F.

Implemented foundation includes:

- Output intent and ICC profile.
- XMP `pdfaid`.
- XMP extension schemas.
- Embedded and associated files.
- `/AFRelationship`.
- Embedded-file MD5 checksum.
- Embedded-file size and modification date.
- Fail-closed creation rules.
- Rule-based validator diagnostics.
- CMake veraPDF integration when veraPDF is available.

### PDF/UA

Creation support currently includes:

- PDF/UA-1.
- PDF/UA-2.
- PDF 2.0 namespace support.
- Tagged PDF.
- MCID allocation.
- Structure tree.
- ParentTree.
- RoleMap.
- Structure attributes.
- `OBJR` for accessible annotations.
- Alternative and actual text.
- Tagged links and file attachments.
- Form accessibility checks.
- Semantic list and table checks.
- Heading hierarchy diagnostics.
- Logical-versus-physical reading-order diagnostics.
- Language and XMP-language consistency.

### Layout

- Rich text runs.
- Paragraph wrapping and alignment.
- Nested lists.
- Tables with auto-width.
- Row span and column span.
- Header-row repetition.
- Page splitting.
- Image flow.
- Area breaks.
- Page-number fields.
- Basic widow, orphan, and keep constraints.

### Fonts and text

- Type1 foundation.
- TrueType parsing and embedding.
- Type3 font writer.
- CID Type0 fonts.
- Horizontal and vertical writing.
- `/DW2` and `/W2`.
- ToUnicode CMaps.
- Basic GSUB and GPOS support.
- Basic ligatures, kerning, mark positioning, Arabic shaping, and bidi foundation.

### Graphics and images

- JPEG baseline encoder.
- PNG Adam7 decoding.
- Inline images.
- Cross-page image deduplication.
- Soft masks and `/Matte`.
- All 16 standard PDF blend modes.
- ICCBased, Separation, and DeviceN writer color spaces.
- Tiling patterns.
- Mesh shading Types 4, 5, 6, and 7.
- Type3 and mesh rendering compatibility through Poppler smoke tests.

### Forms and annotations

- Field hierarchy.
- Text, checkbox, radio, push-button, combo, and list fields.
- Multiline, password, comb, rich-text, editable-combo, and other field flags.
- Appearance generation.
- Form flattening.
- Link, caret, screen, movie, sound, and file-attachment annotations.
- URI, GoTo, GoToR, Launch, Named, and chained actions.

### Build and packaging

- Static and shared builds.
- CMake package export.
- `PdfPP::Core`.
- `find_package(PdfPP 1.0 CONFIG)`.
- Downstream consumer test.
- GCC, Clang, MSVC workflow definitions.
- Strict warning gates.
- ASan and UBSan gates.
- C ABI version symbol and ABI checks.

## 4. Current maturity assessment

Pdf++ is currently best described as:

> **Feature-rich technical preview / early beta**

Estimated practical parity:

| Scope | Estimated parity |
|---|---:|
| Common PDF creation and editing | 55–65% |
| API breadth versus iText Core | 45–55% |
| Encryption and incremental editing | 65–75% |
| Layout | 45–55% |
| Forms and annotations | 50–60% |
| Fonts and text shaping | 35–45% |
| Renderer | 35–45% |
| Digital signatures and PAdES | 35–45% |
| PDF/A and PDF/UA compliance depth | 30–40% |
| Enterprise production reliability | 25–35% |
| Overall practical parity with iText Core | approximately 40% |

The largest limitation is no longer missing API names. The main gaps are correctness depth, corpus coverage, external conformance, signature trust validation, Unicode shaping, rendering accuracy, and production hardening.

## 5. Development principles from this point forward

1. Prefer correctness over feature count.
2. Every bug fix must add a permanent regression fixture.
3. Valid documents must be checked by independent tools.
4. Invalid fixtures must fail for the expected rule, not only return a generic error.
5. Reader limits must fail closed on suspicious input.
6. PDF/A and PDF/UA claims must not be made without external validation.
7. No public API should expose STL types through the future C ABI.
8. Core C++ ownership must remain RAII-based.
9. Strict, sanitizer, and corpus gates must remain mandatory.
10. Public version remains `1.0.0` until release criteria are reached.

# 6. Recommended next batches

## Batch 10 — Conformance infrastructure and corpus

This should be the immediate next implementation batch.

### Goals

- Make veraPDF mandatory in CI.
- Pin a known veraPDF release.
- Generate fixtures for every supported PDF/A and PDF/UA profile.
- Add positive and negative conformance corpora.
- Add qpdf, Poppler, and MuPDF compatibility gates.
- Add coverage reporting.
- Add initial libFuzzer targets.
- Produce `CONFORMANCE_STATUS.md`.

### Required fixture matrix

#### Positive fixtures

- PDF/A-1A and 1B.
- PDF/A-2A, 2B, and 2U.
- PDF/A-3A, 3B, and 3U.
- PDF/A-4, 4E, and 4F.
- PDF/UA-1.
- PDF/UA-2.
- Combined PDF/A-2A + PDF/UA-1.
- Combined PDF/A-4F + PDF/UA-2.

#### Negative fixtures

At least one fixture for each major violation:

- Encryption.
- Missing output intent.
- Invalid ICC profile.
- Missing XMP identification.
- Missing embedded font.
- Missing ToUnicode.
- Invalid associated file metadata.
- Invalid annotation flags.
- Untagged real content.
- Missing alternative text.
- Invalid ParentTree.
- Duplicate MCID.
- Incorrect structure role.
- Invalid table hierarchy.
- Broken form accessibility.
- Incorrect language metadata.
- Invalid namespace.
- Broken XMP extension schema.

### Completion criteria

- All claimed-valid fixtures pass veraPDF.
- All invalid fixtures fail the intended rule.
- CI must fail if veraPDF is unavailable.
- Reports are uploaded as CI artifacts.
- No corpus crash under ASan and UBSan.

## Batch 11 — Parser and security hardening

### Fuzz targets

- Lexer.
- Object parser.
- Dictionary and array parser.
- Classic xref.
- Xref stream.
- Hybrid xref.
- Object stream.
- Content stream.
- Filter pipeline.
- CMap.
- TrueType.
- CFF.
- ICC.
- XMP.
- CMS and ASN.1.
- Incremental revision resolver.

### Security limits

- Maximum recursion.
- Maximum dictionary and array size.
- Maximum object count.
- Maximum page-tree depth.
- Maximum structure-tree depth.
- Maximum object-stream entries.
- Maximum filter-chain length.
- Maximum image dimensions and pixels.
- Maximum ICC and XMP size.
- `/Prev` cycle detection.
- Xref and page-tree cycle detection.
- Checked arithmetic for every offset and length.

### Completion criteria

- No crash on public malformed-PDF corpora.
- Parser/filter/security line coverage at least 80%.
- Parser/filter/security branch coverage at least 70%.
- Fuzz regressions preserved permanently.

## Batch 12 — Production PDF/A creation

### High-level archive API

```cpp
PdfArchiveDocument document;
document.SetProfile(PdfAProfile::PdfA4F);
document.SetLanguage("vi-VN");
document.SetTitle("FEA Calculation Report");
document.SetOutputIntentFromIccFile("sRGB.icc");
document.AddAssociatedFile(...);
document.Save("report.pdf");
```

### Automatic remediation

- Automatic font embedding.
- Automatic ToUnicode generation.
- Output-intent creation.
- Device-color conversion policy.
- Metadata synchronization.
- XMP extension-schema generation.
- Annotation appearance generation.
- Encryption rejection.
- Unsupported-action rejection.
- Associated-file normalization.
- Transparency handling for PDF/A-1.
- Preflight before writing.
- Validation after writing.

### Completion criteria

Every supported profile generated by the high-level API must pass veraPDF without manual dictionary editing.

## Batch 13 — Automatic PDF/UA tagging from layout

Layout objects must create semantic structure automatically:

- Paragraph → `P`.
- Heading → `H1`–`H6`.
- List → `L`, `LI`, `Lbl`, `LBody`.
- Table → `Table`, `TR`, `TH`, `TD`.
- Image → `Figure`.
- Link → `Link`.
- Footnote → `Note`.
- Page number and decorative content → Artifact.

### Required capabilities

- Automatic MCID.
- ParentTree generation.
- Logical reading order.
- Table header associations.
- List labels.
- Language inheritance.
- Automatic `/Alt` validation.
- Repeated table-header policy.
- Tagged links and form widgets.
- Tagged multi-page tables.

## Batch 14 — Matterhorn and accessibility hardening

### Machine-checkable rules

- Complete structure validation.
- Reading-order diagnostics.
- Annotation and form accessibility.
- Formula and figure alternatives.
- Table regularity and header associations.
- List structure.
- Language changes.
- Tab order.
- Artifact correctness.
- Optional-content accessibility.
- Duplicate and orphan structure objects.

### Human-check report

Generate explicit review items for:

- Alternative-text quality.
- Logical reading order.
- Color contrast.
- Heading appropriateness.
- Table-header meaning.
- Link-purpose clarity.
- Form-instruction clarity.
- Decorative-versus-informative images.

### Completion criteria

- Machine-checkable Matterhorn checkpoints covered.
- Human-check report is actionable and page/object specific.
- Screen-reader test procedure documented.

## Batch 15 — Certificate path, OCSP, and CRL validation

### Certificate path

- Verify every certificate signature.
- Build path to trusted root.
- Basic Constraints.
- Key Usage.
- Extended Key Usage.
- Certificate Policies.
- Name Constraints.
- Authority and Subject Key Identifier.
- Path-length constraints.
- Validation at signing time.
- Custom and operating-system trust stores.

### OCSP

- Parse and verify responses.
- Responder authorization.
- Nonce.
- `thisUpdate`, `nextUpdate`, and `producedAt`.
- Delegated responders.
- Revocation status.

### CRL

- Verify CRL signature.
- Delta CRL.
- Indirect CRL.
- Revocation reason.
- Freshness policy.
- Cache.

## Batch 16 — Complete PAdES B/T/LT/LTA

High-level API:

```cpp
PdfPadesSigner signer;

signer.SignBaselineB(...);
signer.SignBaselineT(...);
signer.UpgradeToBaselineLT(...);
signer.UpgradeToBaselineLTA(...);
```

Required:

- RFC 3161 timestamp requests and validation.
- Baseline B and T.
- DSS/VRI LT upgrade.
- Document timestamp for LTA.
- Renewal timestamp.
- Multi-signature revisions.
- DocMDP.
- FieldMDP.
- Modification classification.
- External signing.
- HSM and KMS callbacks.

## Batch 17 — HarfBuzz and font subsetting

### Optional HarfBuzz backend

- Unicode Bidirectional Algorithm.
- Script segmentation.
- Language-system selection.
- Full contextual GSUB.
- Full GPOS.
- Arabic.
- Indic.
- Thai.
- Khmer.
- Myanmar.
- Grapheme clusters.
- Vertical alternates.

### Fonts

- CFF subsetting.
- CFF2 support.
- Composite glyph closure.
- Variable-font instances.
- Ligature ToUnicode.
- Multi-code-point clusters.
- Vertical CID metrics.

## Batch 18 — Renderer correctness

- JBIG2.
- JPEG arithmetic.
- JPX/OpenJPEG.
- ICC matrix and LUT transforms.
- DeviceN and Separation color management.
- Overprint.
- Rendering intents.
- Transfer functions.
- Luminosity soft masks.
- Transparency-group isolation and knockout.
- Internal Type 6/7 mesh rendering.
- Optional-content visibility.
- Font hinting.

### Visual regression

Compare output against:

- MuPDF.
- PDFium.
- Poppler.

Measure:

- Pixel difference.
- Structural similarity.
- Bounding-box mismatch.
- Text-position mismatch.

## Batch 19 — API, ABI, packaging, and documentation

- README.
- License.
- Changelog.
- Security policy.
- Contribution guide.
- Generated API documentation.
- Examples.
- ABI baseline.
- Binary compatibility checker.
- vcpkg.
- Conan.
- NuGet package for C ABI/C# bindings.
- Signed release artifacts.
- SBOM.
- Reproducible builds.
- Windows, Linux, macOS, x64, ARM64.

## Batch 20 — Performance and release audit

### Benchmarks

- Open large PDFs.
- Parse xref and object streams.
- Text extraction.
- Regex search.
- Page import.
- Incremental save.
- Large tables.
- Image-heavy documents.
- Rendering.
- Signing and validation.

### Final release gates

- External conformance.
- Fuzzing.
- Sanitizers.
- ABI compatibility.
- Cross-platform CI.
- Corpus compatibility.
- Visual regression.
- Performance regression.
- Documentation completeness.
- Security review.

## 7. Immediate implementation scope

The next concrete implementation should be **Batch 10**, with this fixed scope:

1. Mandatory pinned veraPDF CI.
2. Full profile fixture matrix.
3. At least 50 negative fixtures.
4. qpdf, Poppler, and MuPDF differential checks.
5. libFuzzer targets for object parser, xref, and filter pipeline.
6. Coverage reporting.
7. CI artifact collection.
8. `CONFORMANCE_STATUS.md`.
9. Regression-preserving failure corpus.
10. Cumulative ZIP packaging after all gates pass.

## 8. Target progression

| Milestone | Expected practical parity |
|---|---:|
| Current Batch 9 | approximately 40% |
| After Batch 10–12 | 50–55% |
| After Batch 13–14 | 58–63% |
| After Batch 15–16 | 65–70% |
| After Batch 17–18 | 72–80% |
| After Batch 19–20 | production-candidate level |

These figures represent practical capability and reliability, not a count of public classes or methods.

## 9. Release definition

Pdf++ should only be described as near iText-level when all of the following are true:

- Claimed PDF/A profiles pass veraPDF across a representative corpus.
- PDF/UA machine-checkable rules pass and human checks are documented.
- PAdES B/T/LT/LTA work across multi-session documents.
- No parser crashes on a substantial malformed corpus.
- Unicode shaping matches HarfBuzz reference output.
- Renderer visual differences are within an agreed threshold.
- Cross-platform strict and sanitizer gates pass.
- ABI stability is tracked.
- Performance is measured and regression-controlled.
- Public documentation and packaging are complete.

Until then, Pdf++ should remain labeled as a technical preview or beta despite the fixed public version string `1.0.0`.
