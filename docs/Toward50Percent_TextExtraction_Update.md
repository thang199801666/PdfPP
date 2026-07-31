# Pdf++ — Toward 50% iText: Text Extraction Update

## Scope

This update converts the content-event foundation into a public positioned-text API and integrates it into `PdfDocument` extraction.

## Added

- `PdfTextChunk` with text, start/end positions, bounding box, font resource, font size, rendering mode, and source content-stream object number.
- `PdfTextExtractor` with Simple, Location, and Region extraction strategies.
- Region filtering using intersecting or fully-contained text chunks.
- Location-based line ordering and word-gap reconstruction.
- Public `PdfDocument::ExtractTextChunks()` and strategy-based `PdfDocument::ExtractText()`.
- Existing `ExtractTextPage()` now uses the new content processor/text chunk pipeline.
- `TJ` numeric adjustments are preserved in content events and used when estimating text advance.

## Compatibility

Existing `GetPageText`, `ExtractTextPage`, and `GetAllPageText` APIs remain available.

## Validation

- Core library builds with C++20.
- Sample builds.
- Unit, reader integration, and writer smoke tests pass: 3/3.

## Remaining work toward 50%

1. Resolve page font dictionaries and `/ToUnicode` streams per font resource.
2. Implement Type 0/CID font widths and exact glyph displacement.
3. Apply text and current transformation matrices correctly.
4. Add vertical writing, rotated text, and form XObject recursion.
5. Replace remaining legacy text scanner code after regression coverage is expanded.
6. Continue reader-kernel split and true random-access parsing.
