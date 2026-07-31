# Pdf++ 0.8.0 — Performance Foundation

This update establishes measurable performance infrastructure and bounded reader state.

## Implemented

- Configurable LRU indirect-object cache (`PdfReaderLimits::maxCachedObjects`).
- Cache capacity diagnostics on `PdfDocument`.
- File-backed parallel page text extraction with independent worker documents.
- Deterministic sequential fallback for memory/stream inputs.
- Visual Studio/CMake `Pdf++.Benchmarks` executable.
- CSV workloads: open/page count, sequential extraction, parallel extraction, image enumeration.
- Integration tests for cache capacity and parallel/sequential equivalence.

## Benchmark usage

```powershell
Pdf++.Benchmarks.exe input.pdf 10 8 > pdfpp_results.csv
```

Arguments are input path, measured iterations, and worker count. Use Release x64 for comparisons.

## Important limitation

The parser still materializes the complete source file in `PdfDocument::bytes_`. This update bounds parsed-object retention and adds concurrency/measurement, but it does not yet claim full random-access parsing. The next reader milestone must replace direct vector access with a random-access byte reader throughout header/xref/object parsing.
