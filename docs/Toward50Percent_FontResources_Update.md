# Toward 50% iText — Font resources update

This batch adds a typed font-resource layer between PDF page resources and text extraction.

Implemented:

- `PdfFontResource` public API.
- Type 1, TrueType, Type 0, CIDFontType0, CIDFontType2 and Type 3 subtype recognition.
- `/BaseFont`, `/Encoding`, `/BaseEncoding` and `/Differences` parsing.
- A practical Adobe-glyph-name-to-Unicode subset with `uniXXXX` and `uXXXXX` fallback.
- Simple-font `/FirstChar` and `/Widths` handling.
- Composite-font `/DescendantFonts`, `/DW` and both forms of `/W` handling.
- `/ToUnicode` stream parsing through `PdfToUnicodeCMap`.
- Identity-H and Identity-V fallback decoding when a ToUnicode map is absent.
- Resolver callback support for indirect font dictionaries, encodings, CMaps and descendants.

The next integration step is to resolve `/Resources /Font` from each page and pass the selected `PdfFontResource` into `PdfTextExtractor`, replacing heuristic character-count widths with actual glyph widths.
