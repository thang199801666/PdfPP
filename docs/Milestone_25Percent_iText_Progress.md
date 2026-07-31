# Pdf++ Milestone toward 25% iText Core coverage

## Scope of this update

This update strengthens the two subsystems that most directly affect text extraction reliability:

1. ToUnicode CMap parsing and decoding.
2. General content-stream tokenization and event processing.

It is an incremental milestone toward the 25% functional-coverage target. It does not claim 25% production parity with iText yet.

## ToUnicode improvements

`PdfToUnicodeCMap` now supports:

- Multiple `begincodespacerange` blocks.
- Mixed one-byte through four-byte source codes.
- Multiple `beginbfchar` and `beginbfrange` blocks.
- Sequential `bfrange` destinations.
- Array-form `bfrange` destinations.
- UTF-16BE conversion.
- Surrogate-pair conversion to four-byte UTF-8.
- Longest valid code-space matching during decode.
- Mapping keys that preserve source-code byte width.

Remaining work includes `begincidchar`, `begincidrange`, `usecmap`, predefined CMaps, vertical writing, and integration with resolved font resources.

## Content processor improvements

`PdfContentProcessor` now includes a real operand stack and recognizes:

- Literal strings with nested parentheses.
- Escaped characters, line continuation, and octal escapes.
- Hexadecimal strings.
- Names with `#xx` escaping.
- Arrays containing strings, names, and numbers.
- Comments and PDF whitespace.
- Text operators: `BT`, `ET`, `Tf`, `Tc`, `Tw`, `Tz`, `TL`, `Tr`, `Ts`, `Tm`, `Td`, `TD`, `T*`, `Tj`, `TJ`, `'`, `"`.
- Graphics-state operators: `q`, `Q`, `cm`.
- Path operators and clipping events.
- XObject invocation through `Do`.
- Unknown-operator events instead of silently discarding operators.

Events now carry a `PdfTextStateSnapshot` containing font resource, size, spacing, scaling, leading, rendering mode, rise, and text matrix.

## Tests

The unit suite now covers:

- Mixed one-byte and two-byte code spaces.
- Array-form `bfrange`.
- Sequential `bfrange`.
- BMP Unicode and surrogate-pair Unicode.
- `TJ` arrays.
- Font and text-matrix events.
- Quote text operators.
- XObject events.

CMake validation result:

```text
3/3 tests passed
0 tests failed
```

## Next recommended implementation batch

1. Integrate the new content processor into page text extraction.
2. Add a font-resource factory for Type 1, TrueType, Type 0, CIDFontType0, and CIDFontType2.
3. Resolve `/ToUnicode`, `/Encoding`, `/Differences`, `/DescendantFonts`, `/W`, and `/DW` from typed dictionaries.
4. Emit positioned `PdfTextChunk` events using text and graphics matrices.
5. Extract `PdfXrefParser`, `PdfXrefIndex`, and `PdfPageTree` from `PdfDocument.cpp`.
6. Replace full-file `bytes_` ownership with random-access reader-backed parsing.
7. Add corpus fixtures for xref streams, object streams, hybrid xref, incremental updates, and CID fonts.

## Estimated target progress

After this update, the library remains below 25% production parity with iText Core. Its reader/text foundation is stronger, but the remaining work for the target is primarily integration, robust font resolution, random-access reader refactoring, generic writer serialization, image support, and existing-document manipulation.
