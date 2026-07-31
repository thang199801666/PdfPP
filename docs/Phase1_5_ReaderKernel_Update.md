# Pdf++ Reader Kernel Update

## Scope

This update continues Milestone 0.5, primarily Phase 2 and Phase 3 foundations.

## Implemented

### Internal object resolver

- Added `Internal::PdfObjectResolver`.
- Moved typed indirect-object parsing and cache ownership out of `PdfDocument.cpp`.
- Cache keys now contain both object number and generation number.
- Added cyclic-reference detection.
- Enforced the configured recursion limit.
- Enforced `PdfReaderLimits::maxObjectCount` while resolving typed objects.
- Added cache inspection and explicit cache clearing:

```cpp
std::size_t PdfDocument::GetCachedObjectCount() const noexcept;
void PdfDocument::ClearObjectCache() const noexcept;
```

### Random-access input abstraction

`PdfInputSource` now exposes:

```cpp
virtual std::uint64_t Size() const = 0;
virtual void Read(std::uint64_t offset, std::span<char> destination) = 0;
virtual std::vector<char> ReadAll();
```

The existing `ReadAll()` workflow remains available for compatibility. File, memory, and stream sources implement the new interface.

This is the required foundation for:

- memory-mapped files;
- reading only the PDF tail when locating `startxref`;
- lazy indirect-object reads;
- lower peak memory on large files.

### Tests

The test suite now verifies:

- random-access memory reads;
- opening a generated valid one-page PDF;
- version and page-tree parsing;
- page geometry;
- text extraction;
- generation-aware object caching;
- cache clearing;
- malformed input failure.

## Compatibility

- Existing `PdfDocument::Open(...)` overloads are preserved.
- Existing reader behavior remains unchanged.
- Visual Studio remains configured for C++20, x64, and Platform Toolset v145.
- `PdfDocument` is explicitly movable and non-copyable, matching document ownership semantics.

## Next recommended implementation

1. Store `PdfInputSource` inside document state instead of immediately calling `ReadAll()`.
2. Add `PdfByteReader` with bounded reads and endian helpers.
3. Move `findStartXref`, classic xref parsing, xref-stream parsing, and `/Prev` traversal into `Internal::PdfXrefParser`.
4. Replace the public `PdfXrefEntry` declaration with a fully internal xref index.
5. Add a file mapping implementation for Windows using `CreateFileMapping` and `MapViewOfFile`.
6. Add regression fixtures for classic xref, xref stream, hybrid xref, and object streams.
