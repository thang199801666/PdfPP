# Pdf++ Page Organization Update

Added incremental page-tree operations for flat page trees:

- ReorderPages
- RemovePages
- ExtractPages
- SplitEvery

The implementation preserves the original PDF bytes and appends a revised `/Pages` root, xref subsection, and trailer with `/Prev`. Output files can be reopened by Pdf++. Nested page trees are detected and currently reported as unsupported instead of being rewritten unsafely.

Version: 0.13.0
