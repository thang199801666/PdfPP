# Pdf++ Phase 6 to Phase 10 Update

This Visual Studio milestone adds a working vertical slice across roadmap phases 6-10.

## Phase 6 - Font and Unicode foundation
- `PdfFontSubtype` and `PdfFontDescriptor`.
- `PdfToUnicodeCMap` with codespace, `bfchar`, and sequential `bfrange` decoding.
- UTF-8 output for common BMP mappings.

## Phase 7 - General content processor foundation
- Event model for begin/end text, rendered text, graphics-state save/restore, and future path events.
- Literal-string decoding for `Tj` operations.
- Designed so text extraction can migrate away from ad-hoc scanning incrementally.

## Phase 8 - PDF writer foundation
- Classic PDF 1.7 output.
- Object numbering, stream lengths, xref table, trailer, and `startxref`.
- Base-14 Helvetica resource.
- Rewrite save mode; incremental save explicitly rejected until safely implemented.

## Phase 9 - Page manipulation foundation
- Add, insert, remove, and move pages before save.
- Per-page media box and content stream.

## Phase 10 - Low-level canvas foundation
- Graphics-state save/restore.
- RGB stroke/fill colors and line width.
- Move/line/rectangle, stroke/fill.
- Begin/end text, font size, text positioning, and escaped literal text.

## Validation
The end-to-end writer test creates a two-page PDF, draws a stroked rectangle and text, saves it, reopens it through the existing reader, checks the page count, and verifies extracted text.

Current CTest result: 3/3 tests passed.

## Deliberately deferred
- Full composite-font resource parsing and surrogate pairs.
- `TJ`, image, path, clipping, and graphics-state matrices in the event processor.
- Xref streams and incremental writer output.
- Copying pages from an existing reader document.
- Advanced color spaces, curves, dash patterns, and resource deduplication.
