# Existing PDF Stamp and Watermark Update

Version 0.11.0 adds incremental text stamping and watermarking for PDFs opened from disk.

## APIs

- `PdfPageEditor::AddTextStamp`
- `PdfPageEditor::AddTextStampToAllPages`
- `PdfPageEditor::AddWatermark`
- `PdfPageEditor::AddWatermarkToAllPages`

The editor appends new content streams, installs page-local Base-14 font and ExtGState resources, preserves existing page content/resources, and writes an incremental xref/trailer revision. Both foreground and background layers are supported.

## Current limits

- Base-14 Helvetica is used for existing-document text stamps.
- Encrypted PDFs are not supported by incremental page editing.
- Multiple different opacity values on the same page currently share one stamp graphics state.
- Image stamps on existing PDFs are planned for the next editing batch.
