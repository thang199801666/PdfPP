# Pdf++ 1.0 release criteria

`1.0.0` is the fixed public version and introduces ABI version 1. Further development keeps the same public version until every capability and performance gate in this document is satisfied.
The stable `1.0.0` tag must not be created until all gates below pass on Windows and Linux.

## Required gates

- Strict GCC, Clang and MSVC builds with warnings treated as errors.
- AddressSanitizer and UndefinedBehaviorSanitizer test runs.
- All writer fixtures accepted by `qpdf --check` and at least one independent renderer.
- No known crash on the maintained malformed-PDF corpus.
- Benchmark regression within the limits documented in `PerformanceContract.md`.
- Public headers reviewed for source compatibility and ABI version set to `1`.
- Renderer limitations documented without claiming full PDF graphics compliance.

## Performance positioning

Pdf++ aims to approach leading libraries for native C++ reader/writer and repeated text-search
workloads. It does not claim parity with MuPDF or PDFium rendering until a common public corpus
and equivalent-output benchmark demonstrates it.
