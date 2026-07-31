# Pdf++ 0.12.0 — Existing PDF Image Stamp

This update adds incremental image stamping to existing PDFs.

## Public API

- `PdfPageEditor::AddImageStamp(...)`
- `PdfPageEditor::AddImageStampToAllPages(...)`
- `PdfPageEdit::imageStamps`

## Supported image sources

- Raw DeviceGray, DeviceRGB, and DeviceCMYK samples.
- JPEG pass-through using `/DCTDecode`.

## Incremental update behavior

The original PDF bytes are preserved. The editor appends image XObject streams,
per-stamp ExtGState resources, page content streams, revised page dictionaries,
and a new xref/trailer revision.

Each image stamp gets its own opacity resource, so stamps with different opacity
values on the same page do not overwrite each other.

## Validation

Writer smoke tests create an existing PDF, add a semi-transparent bordered RGB
image stamp, reopen the result, extract the image, and validate dimensions and
page-space bounding box.
