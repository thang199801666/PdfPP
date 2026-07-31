# Pdf++ 0.15.0 — Object serializer refactor and page import

## Refactor

PDF object serialization was moved out of `PdfPageOrganizer.cpp` into the internal
`PdfObjectSerializer` service. It serializes typed null, boolean, number, name,
string, array, dictionary, indirect-reference, and stream objects. The serializer
is now shared by page-tree incremental updates and fresh-document object imports.

## Page import

The new public `PdfPageImporter` supports:

- Merging all pages from multiple PDF files.
- Copying selected pages from multiple sources in an arbitrary order.
- Cloning the complete reachable object graph for each selected page.
- Remapping object numbers and generations into a clean destination namespace.
- Preserving shared resources without importing them repeatedly.
- Preserving raw stream bytes and filter dictionaries.
- Materializing inherited MediaBox, CropBox, Rotate, and Resources.
- Flattening imported pages under a new destination Pages root.

## Current limitations

- Encrypted source PDFs are rejected.
- AcroForm field-tree merging and named-destination reconciliation are not yet
  specialized; objects reachable from a selected page are copied, but document-
  level name trees are not merged.
- Outlines, document metadata, optional-content properties, structure trees, and
  catalog-level JavaScript are not copied by the page importer.
- Interactive references to pages that were not selected can be imported as
  detached objects and should be normalized in a later link/destination pass.
- Output currently uses a classic xref table and PDF 1.7 header.
