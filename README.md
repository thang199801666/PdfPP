# Pdf++

> **Project status:** Public beta. The API may change before 1.0, and the renderer is experimental. Do not process arbitrary untrusted uploads without sandboxing and strict resource limits.

Pdf++ is a modern C++20 library for reading, creating, modifying, searching, and rendering PDF documents.

## Current capabilities

- Classic xref tables, xref streams, and object streams
- Page inspection, import, merge, split, reorder, remove, and editing
- Text extraction, search, and keyword highlighting
- Canvas drawing, images, Base-14 fonts, and embedded TrueType fonts
- Metadata, hierarchical bookmarks, annotations, and basic AcroForm workflows
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
#include <CPPPdf/CPPPdf.hpp>

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

Version 0.x is suitable for controlled application workflows. Encryption, digital signatures, PDF/A, PDF/UA, advanced layout, and complete complex-script shaping are not yet implemented.

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

## Contributing and security

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md), follow the [Code of Conduct](CODE_OF_CONDUCT.md), and report security-sensitive defects through the private process described in [SECURITY.md](SECURITY.md).

## License

Pdf++ is licensed under the [GNU General Public License v3.0](LICENSE).
Third-party components retain their respective licenses; see [NOTICE](NOTICE)
and the files under `third_party/`.
