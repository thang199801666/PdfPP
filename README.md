# Pdf++

> **Project status:** Public beta. The API may change before 1.0, and the renderer is experimental. Do not process arbitrary untrusted uploads without sandboxing and strict resource limits.

Pdf++ is a modern C++20 library for reading, creating, modifying, searching, and rendering PDF documents.

## Current capabilities

- Classic xref tables, xref streams, and object streams
- Page inspection, import, merge, split, reorder, remove, and editing
- Text extraction, search, and keyword highlighting
- Canvas drawing, images, Base-14 fonts, and embedded TrueType fonts
- Metadata, hierarchical bookmarks, annotations, and basic AcroForm workflows
- AES-128/RC4-128 password encryption, permissions, password change and removal
- File, memory, stream, and custom input sources

See [docs/FeatureMatrix.md](docs/FeatureMatrix.md) for the supported feature scope and known limitations.

## Requirements

- C++20 compiler
- CMake 3.24 or newer
- Visual Studio 2026 with MSVC toolset v145 for the canonical Windows solution; GCC or Clang for CMake-based builds

## Build

```bash
cmake -S . -B build -DPDFPP_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

To enforce a warning-clean core build:

```bash
cmake -S . -B build-strict \
  -DPDFPP_WARNINGS_AS_ERRORS=ON \
  -DPDFPP_BUILD_SAMPLES=OFF \
  -DPDFPP_BUILD_BENCHMARKS=OFF
cmake --build build-strict
```

## Minimal example

```cpp
#include <CPPPdf/CPPPdf.h>

int main() {
    CPPPdf::PdfWriter writer;
    const auto page = writer.AddPage(CPPPdf::PdfPageSize::A4());
    writer.GetCanvas(page)
        .BeginText()
        .SetFont(CPPPdf::PdfStandardFont::Helvetica, 14)
        .MoveTextPosition(72, 770)
        .ShowText("Hello from Pdf++")
        .EndText();
    writer.Save("output.pdf");
}
```

## Stability status

Version 0.x is suitable for controlled application workflows. AES-256/public-key encryption, digital signatures, PDF/A, PDF/UA, advanced layout, and complete complex-script shaping are not yet implemented.

## Password encryption

```cpp
CPPPdf::PdfEncryptionOptions encryption;
encryption.userPassword = "reader-password";
encryption.ownerPassword = "owner-password";
encryption.permissions.copy = false;

CPPPdf::PdfWriter writer;
writer.SetEncryption(encryption); // AES-128 by default
// Add content, then save.
writer.Save("protected.pdf");

CPPPdf::PdfReaderOptions openOptions;
openOptions.password = "reader-password";
auto document = CPPPdf::PdfDocument::Open("protected.pdf", openOptions);

CPPPdf::PdfPasswordManager::ChangePassword(
    "protected.pdf", "changed.pdf", "owner-password", encryption);
CPPPdf::PdfPasswordManager::RemovePassword(
    "changed.pdf", "clear.pdf", "owner-password");
```

Password rewrite requires different input and output paths, which prevents an
interrupted rewrite from destroying the source file. See
[docs/MuPDFGapAnalysis.md](docs/MuPDFGapAnalysis.md) for the remaining parity gaps.

Page edits, annotations, and AcroForm updates accept `PdfReaderOptions` as their final
argument. Encrypted incremental revisions preserve the file ID, encrypt revised
objects, and enforce user-password permission bits. Owner-password authentication
bypasses those restrictions.

- Named destinations and URI/internal link annotations.

- Viewer preferences, page layout/page mode, and custom page labels

### Document actions and print preferences

`PdfWriter` can define the initial page view with `SetOpenAction()` and configure print-oriented viewer preferences such as scaling, duplex mode, PDF-size tray selection, and copy count.

### Embedded files and page attachments

`PdfWriter` can embed arbitrary binary files from memory or disk, publish them through the PDF embedded-files name tree, associate them with the document, and place clickable file-attachment annotations on pages.


## Regex text search

```cpp
auto chunks = document.ExtractTextChunks(0);
CPPPdf::PdfRegexSearchOptions options;
options.caseInsensitive = true;
options.maxMatches = 100;
auto matches = CPPPdf::PdfTextSearch::FindRegex(
    chunks, R"(invoice\s+#?\d+)", options);
```

Regex search uses the C++ ECMAScript grammar and maps each match back to its source chunks and page-space rectangles. For performance-sensitive pipelines, extract chunks once and run multiple searches on that result instead of extracting text again for every pattern. Compile frequently reused patterns once and use the precompiled overload:

```cpp
const std::regex invoicePattern(
    R"(INV-\d{4,})",
    std::regex_constants::ECMAScript |
    std::regex_constants::optimize);

auto matches = CPPPdf::PdfTextSearch::FindRegex(
    chunks, invoicePattern, options);
```

Version 0.32 also removes repeated file `stat/open` work in the file input source, uses allocation-free numeric conversion in parser/serializer hot paths, avoids a full haystack copy for case-sensitive search, and uses prefix sums for constant-time line-barrier checks.

## Test coverage

The public feature-to-test mapping is documented in `docs/TestCoverage.md`.

### Reusable high-performance text search index

For repeated searches on the same extracted page, build an index once:

```cpp
const auto chunks = document.ExtractTextChunks(pageIndex);
CPPPdf::PdfTextSearchIndex index(chunks);

const auto words = index.Find("connection");
const std::regex ids(R"(FEA-\d{6})", std::regex_constants::optimize);
const auto identifiers = index.FindRegex(ids);
```

The reusable index stores compact chunk ranges instead of one mapping record per UTF-8 byte. This reduces preparation work and memory consumption when many queries are executed against the same page.

## Pdf++ 0.40 performance APIs

Version 0.40 adds `PdfTextDocumentIndex` for cached document-wide search, `PdfDocument::OpenMapped()` for opt-in mapped input, and `PdfWriter::Save(std::ostream&)` for direct output sinks.

```cpp
auto document = CPPPdf::PdfDocument::Open("report.pdf");
CPPPdf::PdfTextDocumentIndex index(document);
index.Preload(0, index.GetPageCount());
auto matches = index.FindAll("stress");
```

See `docs/Performance.md` for cache configuration, benchmark usage and the remaining performance gaps to mature rendering engines.


## Pdf++ 0.42 image rendering and anti-aliasing

Version 0.42 extends the CPU renderer with decoded Image XObject and inline-image rendering for DeviceGray, DeviceRGB, and DeviceCMYK sample data. Optional bilinear interpolation is enabled by default. The renderer also supports 1x-4x supersampling with box-filter downsampling through `PdfRenderOptions::antiAliasSamples`. JPEG/JPX decoding remains outside the dependency-free core and will be added through optional codecs.

## Pdf++ 0.41 rendering foundation

Version 0.41 introduces the first CPU rendering backend:

```cpp
const auto document = CPPPdf::PdfDocument::Open("report.pdf");

CPPPdf::PdfRenderOptions options;
options.dpi = 144.0;
const auto bitmap = CPPPdf::PdfPageRenderer::Render(document, 0, options);
bitmap.SavePpm("page-1.ppm");
```

The initial renderer supports RGBA bitmap output, crop/media boxes, page rotation,
CTM transforms, RGB/gray stroke and fill colors, line widths, rectangles, lines,
Bezier curve flattening, path fill/stroke operations, and an ASCII text fallback.
It is a rendering foundation rather than a complete PDF imaging model. Image
XObjects, exact embedded-font glyph rasterization, clipping paths, transparency,
blend modes, patterns, shadings and advanced color spaces remain future work.

## Pdf++ 0.45 embedded CFF glyph rendering

Version 0.45 parses embedded CFF fonts (charset, CharStrings, Private DICT, and
local/global subrs) and rasterizes their Type 2 charstring outlines in the CPU
renderer:

```cpp
const auto document = CPPPdf::PdfDocument::Open("cjk.pdf");
CPPPdf::PdfRenderOptions options;
options.dpi = 144.0;
const auto bitmap = CPPPdf::PdfPageRenderer::Render(document, 0, options);
```

Embedded TrueType and CFF/OpenType glyph outlines are both drawn from font data;
identity-encoded CID fonts map character codes to glyphs directly. Simple-CFF
encoding mapping, hinting, and vertical metrics remain future work.

## Pdf++ 0.46 digital-signature foundation

Version 0.46 adds an external digital-signature foundation. Pdf++ does not embed a
crypto backend; instead it exposes the exact bytes an external signer must digest
and writes the produced signature back into the placeholder:

```cpp
CPPPdf::PdfSignatureManager::Sign(
    "unsigned.pdf", "signed.pdf",
    [](std::span<const std::byte> digestInput) {
        // Feed digestInput to an external CMS/PKCS#7 signer and return the bytes.
        return ProduceSignature(digestInput);
    },
    [](CPPPdf::PdfSignatureFieldOptions& options) {
        options.fieldName = "Approved";
        options.signerName = "Thang Nguyen";
        options.reason = "Release approval";
        return options;
    }());
```

`PrepareForSigning` computes the `/ByteRange`, `ApplySignature` writes the signature
value into the `/Contents` placeholder in place, and `GetSignatures` inspects fields,
ByteRange, and applied contents. CMS/PKCS#7 parsing, key management, and validation
remain external.

## Pdf++ 0.47 PDF/A conformance validation

Version 0.47 adds practical PDF/A validation for parts 1, 2, 3, and 4 across the
A/B/U conformance levels:

```cpp
const auto document = CPPPdf::PdfDocument::Open("archive.pdf");
const auto result = CPPPdf::PdfConformanceValidator::Validate(
    document, CPPPdf::PdfConformanceProfile::PdfA2B);
if (!result.IsValid()) {
    for (const auto& issue : result.issues) {
        // Report issue.code / issue.message, skipping non-fatal warnings.
    }
}
```

The validator checks the file version header, forbids encryption, verifies the XMP
metadata `pdfaid:part`/`conformance`, requires a `/GTS_PDFA1` output intent, validates
embedded TrueType/CFF fonts, enforces ToUnicode (level U) and tagged structure
(level A), rejects transparency for PDF/A-1, and flags forbidden annotation subtypes.

## Pdf++ 0.48 fuzzing

Version 0.48 adds Clang libFuzzer targets for the parser, content processing, stream
filters, and embedded fonts:

```text
cmake -S . -B build-fuzz -DPDFPP_BUILD_FUZZERS=ON -DPDFPP_BUILD_TESTS=OFF
cmake --build build-fuzz --config Release
PdfPP.FuzzReader.exe corpus\pdf -artifact_prefix=artifacts\fuzz\
PdfPP.FuzzContent.exe corpus\content -artifact_prefix=artifacts\fuzz\
PdfPP.FuzzFilter.exe corpus\streams -artifact_prefix=artifacts\fuzz\
PdfPP.FuzzCffFont.exe corpus\fonts -artifact_prefix=artifacts\fuzz\
PdfPP.FuzzTrueTypeFont.exe corpus\fonts -artifact_prefix=artifacts\fuzz\
```

All harnesses run under AddressSanitizer/UndefinedBehaviorSanitizer. Normal parser
exceptions are expected; sanitizer findings and non-termination are actionable bugs.

## Pdf++ 0.49 line dash patterns

Version 0.49 adds line dash pattern support to the CPU renderer. The content
processor parses the `d` operator into a `SetDashPattern` event, and the renderer
applies the on/off alternation across subpaths with phase offset:

```cpp
writer.GetCanvas(page)
    .SetStrokeColor(CPPPdf::PdfColor::Black())
    .SetLineWidth(3.0)
    .SetDashPattern({8.0, 4.0}, 0.0)   // 8 on, 4 off
    .MoveTo(10, 50).LineTo(150, 50).Stroke();
```

Dashes participate in graphics-state save/restore and transparency-group scoping,
completing the roadmap's line cap/join/miter behavior milestone.

## Contributing and security

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md), follow the [Code of Conduct](CODE_OF_CONDUCT.md), and report security-sensitive defects through the private process described in [SECURITY.md](SECURITY.md).

## License

Pdf++ is licensed under the [GNU General Public License v3.0](LICENSE).
Third-party components retain their respective licenses; see [NOTICE](NOTICE)
and the files under `third_party/`.
