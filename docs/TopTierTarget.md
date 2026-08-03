# Pdf++ 1.0.0 top-tier target

Pdf++ keeps the public version fixed at `1.0.0` while development continues. A release build is considered to have reached the target only when all gates below pass on the same revision.

## Capability gates

- Reader compatibility across classic xref, xref streams, object streams, malformed-but-recoverable files, encrypted-document detection, and bounded resource consumption.
- Accurate text extraction for Latin, CJK, CID fonts, ToUnicode CMaps, rotated text, nested Form XObjects, and multi-stream pages.
- Search support for literal, regex, reusable page indexes, document indexes, Unicode normalization policy, and stable geometry mapping.
- Writer validation for page trees, fonts, images, outlines, links, forms, attachments, incremental structures, and stream output.
- Renderer support for vector paths, clipping, images, real glyph outlines, transparency, blend modes, soft masks, patterns, shadings, annotations, and required color spaces.
- Stable C++20 public API and ABI version 1.

## Quality gates

- Strict GCC, Clang, and MSVC builds with warnings as errors.
- ASan, UBSan, and platform memory diagnostics pass.
- Fuzz targets for object parsing, xref parsing, stream filters, content parsing, fonts, and images.
- Clang libFuzzer targets (`PdfPP.FuzzReader`, `FuzzContent`, `FuzzFilter`, `FuzzCffFont`, `FuzzTrueTypeFont`) run under ASan/UBSan; object parsing, stream filters, content processing, and embedded fonts are covered.
- External validation using qpdf and independent reader smoke tests.
- Public test corpus with licenses suitable for redistribution.
- No known crash on untrusted input within configured limits.

## Performance gates

Benchmarks must run on the same machine, corpus, cache state, output requirements, and thread count.

- Open/page enumeration: no slower than 1.5x the fastest supported comparison library.
- Latin text extraction: no slower than 1.7x MuPDF or PDFium while producing equivalent text and geometry.
- Warm repeated search: equal to or faster than application-level indexes built on comparison libraries.
- Vector rendering: no slower than 2.0x MuPDF for supported operators at equivalent output quality.
- Writer throughput: within 1.5x PoDoFo or iText for equivalent raw-canvas workloads.
- Peak memory: lower than PDFBox and iText on the agreed large-document corpus.
- No accepted benchmark regression above 10% without an explicit documented tradeoff.

The version number is not used as a progress counter. Changes are tracked through the changelog, benchmark baselines, ABI version, and Git history.
