# Pdf++ 1.0.0 benchmark notes

## Vector renderer microbenchmark

Test document: one 300 x 300 point page containing 100 independently painted rectangles.
Output: 96 DPI RGBA bitmap, 400 x 400 pixels. Same host, compiler mode and PDF.

| Version | Median/observed render time |
|---|---:|
| 0.43.0 | 61.20 ms |
| 1.0.0 | 9.47 ms |

The direct clipped-paint path is approximately 6.5x faster for this vector-heavy microbenchmark.
This result is not a general comparison with MuPDF or PDFium. Full renderer parity still requires
accurate glyph rasterization, transparency groups, blend modes, advanced color spaces and a shared corpus.

## Text and reader regression run

The existing 200-page internal corpus remained within the normal run-to-run range. Reusable literal
search remained approximately 0.02 ms on the first-page index, and the full unit-test suite passed.
