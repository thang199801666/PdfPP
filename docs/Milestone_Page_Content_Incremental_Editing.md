# Pdf++ 0.10.0 — Incremental Page Content Editing

This milestone adds general page-content and page-geometry editing for existing, unencrypted PDFs.

## Public API

- `PdfPageEditor::ApplyEdits(...)`
- `PdfPageEditor::AddContent(...)`
- `PdfPageEdit`
- `PdfContentCommands`

## Supported operations

- Prepend a background content stream.
- Append a foreground content stream.
- Preserve existing `/Contents` references in their original order.
- Set page `/Rotate` to 0, 90, 180, or 270 degrees.
- Set `/MediaBox` and `/CropBox`.
- Batch multiple changes to the same page into one page-dictionary revision.
- Write changes using a classic incremental xref section and `/Prev` trailer link.

## Drawing helpers

- Lines.
- Filled rectangles.
- Stroked rectangles.

The helpers emit isolated `q ... Q` graphics-state blocks and require no additional page resources.

## Current limitations

- Encrypted PDFs are rejected.
- Direct stream objects embedded directly in `/Contents` are not yet rewritten.
- Generic resource insertion for fonts, images, and ExtGState is a later milestone.
- Page insertion, deletion, reordering, import, merge, and split remain pending.
