# Pdf++ 0.16.0 — Document Structure Merge

## Summary

This update extends `PdfPageImporter` beyond page-local object graphs. Fresh merged documents can now preserve selected document-level structures from the first source document while remapping every indirect reference into the destination object namespace.

## Preserved structures

- Trailer `/Info` dictionary.
- Catalog `/Metadata` stream.
- Catalog `/Outlines` hierarchy.
- Catalog `/Names` and legacy `/Dests` when all pages from the first source are imported in original order.
- `/PageMode`, `/PageLayout`, `/Lang`, and `/ViewerPreferences`.

Outlines and named destinations are intentionally skipped for partial or reordered imports from the first source because their destinations can target pages that are not present in the output.

## API

```cpp
CPPPdf::PdfPageImportOptions options;
options.preserveDocumentInfo = true;
options.preserveMetadataStream = true;
options.preserveOutlines = true;
options.preserveNamedDestinations = true;
options.preservePageModeAndLayout = true;

const auto result = CPPPdf::PdfPageImporter::MergeDocuments(
    {"first.pdf", "second.pdf"},
    "merged.pdf",
    options);
```

The result reports `preservedCatalogEntryCount` and `preservedDocumentInfo`.

## Reader API additions

```cpp
PdfReference PdfDocument::GetCatalogReference() const;
std::optional<PdfReference> PdfDocument::GetTrailerReference(const PdfName& key) const;
```

These APIs avoid parsing raw trailer/catalog strings in higher-level modules.

## Tests

The writer integration test creates a source PDF containing document information, XMP metadata, an outline tree, page mode/layout, and a destination to a page. It merges that PDF with a nested-page-tree document and verifies:

- The title and author remain available through `GetDocumentInfo()`.
- Catalog structures are present.
- The outline destination points to the remapped first output page.
- All imported pages remain readable.

All three test executables pass.
