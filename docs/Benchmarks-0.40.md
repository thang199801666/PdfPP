# Pdf++ 0.40 benchmark snapshot

This snapshot is a regression signal, not a universal comparison with other PDF libraries.

- Build: Release, strict warnings as errors
- Corpus: synthetic 200-page PDF, approximately 1.5 MB and 13,000 text lines
- Samples: median of five runs unless stated otherwise
- Threads: four for parallel extraction

| Workload | Median |
|---|---:|
| Open and page count | 9.598 ms |
| Sequential text extraction | 76.196 ms |
| Parallel text extraction | 49.447 ms |
| Reusable page literal search | 0.020 ms |
| Reusable page regex search | 0.280 ms |
| Cold document-wide literal search | 103.711 ms |
| Warm document-wide literal search | 31.357 ms |
| Warm document-wide precompiled regex search | 5.418 ms |
| Image enumeration | 57.718 ms |

The normal buffered file input remained the default after mapped-plus-copy input regressed open time on this corpus. `OpenMapped()` is therefore opt-in and should be evaluated on the application's real large-file corpus.
