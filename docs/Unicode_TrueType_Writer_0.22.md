# Pdf++ 0.22 — Embedded TrueType Unicode Writing

## Public API

```cpp
auto font = CPPPdf::PdfTrueTypeFont::Load("C:/Windows/Fonts/arial.ttf");
auto canvas = writer.GetCanvas(page);
canvas.BeginText()
      .SetTrueTypeFontAndSize(font, 14)
      .MoveText(72, 720)
      .ShowTextUtf8(u8"Tiếng Việt – Ελληνικά – 😀")
      .EndText();
```

## Generated PDF structures

- Embedded original TrueType bytes in `/FontFile2`.
- `/FontDescriptor`.
- `/CIDFontType2` with `/CIDToGIDMap /Identity`.
- Type 0 font with `/Encoding /Identity-H`.
- `/ToUnicode` CMap containing the Unicode characters actually used.
- Page-local font resources with stable generated names (`/FT1`, `/FT2`, ...).

## Validation and behavior

- UTF-8 is validated before writing.
- Missing glyphs raise `std::invalid_argument`; no silent character replacement.
- A TrueType font is registered once and reused across pages.
- Existing Base-14 text APIs remain unchanged.

## Current limits

- The entire TrueType file is embedded; font subsetting is not implemented yet.
- Glyph advance widths are currently represented by the CID font default width.
- Complex-script shaping, bidi layout, kerning, variation fonts, and TTC collections are not yet implemented.
- Callers must provide pre-shaped logical text only for scripts that do not require shaping.
