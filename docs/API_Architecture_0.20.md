# Pdf++ Public API Architecture — 0.20.0

## Canonical entry points

Application code should include `CPPPdf/Api.hpp`, or a focused module umbrella:

- `CPPPdf/Reader.hpp`
- `CPPPdf/Document.hpp`
- `CPPPdf/Text.hpp`
- `CPPPdf/Graphics.hpp`
- `CPPPdf/Writer.hpp`
- `CPPPdf/Annotations.hpp`
- `CPPPdf/Forms.hpp`

`CPPPdf/CPPPdf.hpp` and the root compatibility headers remain available for source compatibility.

## API rules

1. Public classes and value types remain in `CPPPdf`.
2. Implementation-only types remain in `CPPPdf::Internal` and are never included by `Api.hpp`.
3. Public methods use PascalCase. Existing lowercase accessors remain compatibility wrappers.
4. Shared geometry and identity types live in `CPPPdf/Types.hpp`.
5. Options are input structs, results are output structs, and all page indices are zero-based.
6. Filesystem APIs accept `std::filesystem::path`; non-owning bytes use `std::span`.
7. Failures use `PdfException` with a stable `PdfErrorCode`.

## Compatibility changes

`PdfPoint` is now a common core type. `PdfStampPoint` remains an alias, so existing stamp code compiles unchanged.
