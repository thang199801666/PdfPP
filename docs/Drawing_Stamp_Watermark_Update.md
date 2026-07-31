# Pdf++ Drawing, Stamp, and Watermark Update

## Version

0.7.0

## Added canvas operations

- Stroke/fill opacity through ExtGState resources.
- Line cap, line join, miter limit, and dash patterns.
- Matrix concatenation.
- Cubic Bezier curves and close-path.
- DrawLine and FillRectangle convenience methods.
- Even-odd fill, fill-stroke, clipping, and end-path operators.
- Explicit text matrix.

## Added writer convenience APIs

- Text stamps with rotation, opacity, background, border, and foreground/background layers.
- Image stamps with opacity, border, and foreground/background layers.
- Text and image stamps applied to all pages.
- Watermarks aligned left/center/right and bottom/middle/top.
- Watermarks applied to one page or all pages.

## Resource handling

Opacity is serialized using deduplicated `/ExtGState` resources with `/CA` and `/ca`. Page resource dictionaries include only the graphics states used by that page.

## Current scope

These APIs operate on pages managed by `PdfWriter`. Applying stamps to an already-opened `PdfDocument` requires the future editable-document/incremental-writer subsystem.
