# Pdf++ 0.21 TrueType Font Foundation

This milestone introduces a public, dependency-free TrueType font parser foundation.

## Public API

- `PdfTrueTypeFont::Load(path)`
- `PdfTrueTypeFont::Parse(bytes, sourceName)`
- `Supports(codePoint)`
- `GetGlyphId(codePoint)`
- `GetGlyphMappingCount()`
- `GetBytes()`

The parser reads the SFNT table directory and Unicode `cmap` format 4 and format 12 subtables. It is designed to feed the next writer milestone: Type0/CIDFontType2 embedding, ToUnicode generation, and UTF-8 text output.

## Current scope

This milestone intentionally does not alter `PdfWriter` serialization. It avoids regressions in the existing stamp, image, opacity, and canvas features while establishing a tested font mapping layer.
