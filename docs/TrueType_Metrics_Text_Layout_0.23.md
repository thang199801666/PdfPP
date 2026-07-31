# TrueType Metrics and Unicode Text Layout — 0.23.0

## Added

- Parses `head`, `maxp`, `hhea`, and `hmtx` TrueType tables.
- Exposes units-per-em, ascent, descent, line gap, glyph count, and glyph advance widths.
- Measures UTF-8 text in PDF user-space units at a requested font size.
- Emits CIDFont `/W` entries based on actual glyph metrics.
- Adds `PdfCanvas::DrawTextUtf8` with bounding-box layout, word wrapping, line spacing, and left/center/right alignment.

## Public API

```cpp
const auto font = PdfTrueTypeFont::Load("font.ttf");
const double width = font.MeasureTextUtf8("Tiếng Việt", 14);
const double lineHeight = font.GetLineHeight(14, 1.2);

PdfTextLayoutOptions layout;
layout.box = {72, 500, 420, 720};
layout.fontSize = 14;
layout.lineSpacing = 1.2;
layout.alignment = PdfTextAlignment::Center;
layout.wrap = true;

writer.GetCanvas(page).DrawTextUtf8(font, "Unicode text...", layout);
```

## Current limits

- Wrapping is word-based and does not yet implement Unicode line-breaking rules.
- No kerning, shaping, bidi reordering, or hyphenation.
- Fonts are still embedded in full; subsetting is the next font milestone.
