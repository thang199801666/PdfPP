# Phase 1-5 structure update

## Completed in this update

- Canonical public API folders: Core, IO, Objects, Document, Filters.
- Private parser header moved out of the installable include tree.
- Compatibility forwarding headers preserve existing includes.
- Umbrella header `<CPPPdf/CPPPdf.hpp>` added.
- `PdfDocument.cpp` moved into the Document implementation module.
- Modern CMake target `CPPPdf::Core` added.
- Install/export package and `find_package(PdfPP CONFIG)` support added.
- Build toggles for samples, tests, installation, and warnings-as-errors added.
- Visual Studio v145 project and filters synchronized with the canonical structure.
- Unit and integration test targets separated.
- Install-tree validation completed.

## Next implementation priority

1. Move xref parsing out of `PdfDocument.cpp` into `Internal::PdfXrefParser`.
2. Introduce `PdfObjectResolver` with generation-aware cache keys and bounded LRU storage.
3. Replace full-file `ReadAll()` ownership with random-access byte readers and memory mapping.
4. Make `PdfPage` a lightweight document-backed handle instead of a copied snapshot.
5. Return typed resource dictionaries and typed content streams from `PdfPage`.
6. Add regression corpus registration for xref streams, object streams, hybrid xref, and damaged files.
