# Pdf++ Performance Guide

## 0.40 performance architecture

Pdf++ 0.40 keeps the normal buffered file source as the default because it is faster for small and medium files on the current benchmark corpus. `PdfDocument::OpenMapped()` is opt-in for workloads where a persistent read-only mapping is useful.

The text pipeline now provides three cache levels:

1. `PdfDocument` indirect-object LRU cache.
2. Per-extraction font-resource cache.
3. `PdfTextDocumentIndex`, which retains extracted page chunks and reusable search indexes under a configurable memory budget.

## Repeated document search

```cpp
CPPPdf::PdfDocument document = CPPPdf::PdfDocument::Open("report.pdf");

CPPPdf::PdfDocumentTextIndexOptions options;
options.memoryBudgetBytes = 256U * 1024U * 1024U;
options.maxConcurrency = 4U;

CPPPdf::PdfTextDocumentIndex index(document, options);
index.Preload(0U, index.GetPageCount());

auto literal = index.FindAll("finite element");

const std::regex jobPattern(
    R"(FEA-\d{6})",
    std::regex_constants::ECMAScript |
    std::regex_constants::optimize);
auto regex = index.FindRegexAll(jobPattern);
```

`GetStatistics()` reports cache hits, misses, estimated bytes, cached pages and evictions.

## Memory mapped input

```cpp
auto document = CPPPdf::PdfDocument::OpenMapped("large.pdf");
```

Mapping remains opt-in. The current parser owns a contiguous document buffer, so mapping avoids repeated file reads but does not yet make all parser objects zero-copy.

## Streaming output sink

```cpp
std::ofstream output("report.pdf", std::ios::binary);
CPPPdf::PdfWriter writer;
// Add pages and content.
writer.Save(output);
```

The output sink is streamed directly to `std::ostream`; complex object bodies are still prepared before final serialization. Full object-by-object streaming and unchanged-stream passthrough remain future work.

## Benchmark discipline

Run:

```text
Pdf++.Benchmarks input.pdf 7 4
```

The benchmark reports open/page count, sequential and parallel extraction, literal and regex search, reusable page index, cold/warm document index, and image enumeration. Compare medians on the same machine, build type, corpus and thread count.

## Remaining gaps to leading engines

Pdf++ 0.40 does not claim parity with MuPDF or PDFium. The largest remaining opportunities are:

- Parser views over mapped input without copying the full file.
- Shared document-level parsed font and CMap cache.
- Independent per-worker resolver contexts for parallel preload.
- Optional RE2 or PCRE2 JIT backend.
- Arena allocation and PDF-name interning.
- True object-by-object writer streaming and unchanged compressed-stream passthrough.
