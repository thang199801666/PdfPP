# Performance contract

Release builds enable IPO/LTO when supported. Portable release binaries keep CPU-specific
instructions disabled; local benchmark builds may set `PDFPP_NATIVE_OPTIMIZATIONS=ON`.

A release candidate fails the performance gate when the median time of a tracked workload
regresses by more than 10% from the accepted baseline on the same host and corpus, unless the
change fixes correctness or security and the regression is documented.

Tracked workloads:

- open and page count
- sequential and parallel text extraction
- reusable literal and precompiled-regex search
- cold and warm document-wide search
- image enumeration
- vector rendering

Renderer hot paths must avoid full-page temporary bitmaps for individual paint operations.
