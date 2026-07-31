# Toward 50% iText — Page Font Integration Update

## Scope

This update connects the previously introduced `PdfFontResource` layer to real page text extraction.

## Implemented

- Resolves inherited page `/Resources` through the page-tree `/Parent` chain.
- Resolves direct and indirect `/Font` resource dictionaries.
- Builds a per-page font-resource map using `PdfFontResource::Create`.
- Uses the active `Tf` resource name to select the page font.
- Decodes content-string bytes through `/Encoding`, `/Differences`, `/ToUnicode`, or Identity encoding.
- Uses `/Widths`, `/W`, and `/DW` values to calculate text advances.
- Retains a safe heuristic fallback for missing, malformed, or unsupported fonts.
- Adds `glyphCount` and `usedEmbeddedFontMetrics` diagnostics to `PdfTextChunk`.
- Adds integration coverage with a generated PDF containing an indirect font resource, `/Differences`, and explicit widths.

## Current limitations

- Composite code segmentation currently assumes two-byte character codes when calculating widths.
- Font descriptor ascent, descent, and bounding boxes are not yet used for vertical bounds.
- Form XObject resource inheritance is not yet processed recursively.
- Font resource maps are currently built per extraction call rather than stored in a bounded document cache.
- The page tree and resource discovery still share the monolithic `PdfDocument.cpp` implementation.

## Next recommended batch

1. Add reusable document-level font cache keyed by indirect reference.
2. Add CMap-aware character-code iteration for variable-width source codes.
3. Apply CTM and graphics-state matrices to text chunk coordinates.
4. Process Form XObjects recursively with their own resource dictionaries.
5. Use font descriptor ascent/descent for more accurate bounding boxes.
