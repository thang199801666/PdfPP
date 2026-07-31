# Keyword highlight example

Project: `Pdf++.HighlightSample`

Run from Visual Studio or command line:

```powershell
Pdf++.HighlightSample.exe input.pdf output_highlighted.pdf
```

The example searches every page for `openXL` using ASCII case-insensitive comparison and appends PDF Highlight annotations using a light-yellow color.

The output is written as an incremental update. Existing page content and previous document bytes are preserved.

Main API:

```cpp
CPPPdf::PdfKeywordHighlightOptions options;
options.keyword = "openXL";
options.caseInsensitive = true;
options.color = {1.0, 1.0, 0.70};
options.opacity = 0.35;

const auto result = CPPPdf::PdfKeywordHighlighter::HighlightFile(
    inputPath,
    outputPath,
    options);
```

Current limitations:

- Encrypted PDFs are not supported by incremental highlighting yet.
- Matching is performed inside each rendered text chunk. A word split across unrelated content events may require the future page-level glyph index.
- Highlight geometry is based on extracted text bounding boxes.
