# Pdf++ text search and multi-chunk highlighting update

Version: 0.7.1

## Added

- Public `PdfTextSearch` API.
- Case-sensitive and ASCII case-insensitive searching.
- Matching across adjacent text chunks on the same visual line.
- Geometry mapping from every matched byte range back to one or more PDF rectangles.
- Multi-quad highlight annotations for keywords split across `Tj`/`TJ` operations, fonts, or content events.
- Configurable line tolerance and maximum horizontal gap.

## Behavior

A keyword such as `openXL` is now found when extraction produces separate chunks such as `open` and `XL`, provided they are visually adjacent on the same line. One highlight annotation is written with multiple `/QuadPoints`, avoiding a single oversized rectangle across unrelated content.

Matches do not cross a visual line break by default.
