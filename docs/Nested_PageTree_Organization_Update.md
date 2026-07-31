# Nested Page Tree Organization Update

Version: 0.14.0

## Implemented

- Reorder, extract, remove, and split now accept PDFs with nested `/Pages` nodes.
- Selected page dictionaries are revised so `/Parent` points to the document page-tree root.
- Inherited `/MediaBox`, `/CropBox`, `/Rotate`, and `/Resources` values are materialized before reparenting.
- Incremental xref output supports multiple revised objects and discontiguous object-number subsections.
- Original PDF bytes and all referenced content/font/image objects remain unchanged.

## Validation

An integration fixture uses a two-level page tree with different inherited media boxes and font resources. Reorder and extraction preserve text extraction and page geometry.

## Remaining work

- Full page import and merge between different documents.
- Rebalancing very large page trees instead of flattening selected pages under the root.
- Generic object graph cloning with reference remapping.
