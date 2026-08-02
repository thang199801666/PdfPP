# Pdf++ Performance Guide

## 0.40 performance architecture

Pdf++ 0.40 keeps the normal buffered file source as the default because it is faster for small and medium files on the current benchmark corpus. `PdfDocument::OpenMapped()` is opt-in for workloads where a persistent read-only mapping is useful.

The reader and text pipeline now provide five cache levels:

1. `PdfDocument` indirect-object LRU cache.
2. A decoded `/ObjStm` LRU cache with independent count and byte budgets.
3. Per-extraction font-resource cache.
4. A document-level indirect font-resource LRU; parsed ToUnicode CMaps are retained inside the cached font resource.
5. `PdfTextDocumentIndex`, which retains extracted page chunks and reusable search indexes under a configurable memory budget.

The reader also limits the serialized size of each materialized indirect object
(`PdfReaderLimits::maxIndirectObjectBytes`, 256 MiB by default). This protects
large-file and untrusted-input workloads from malformed xref entries that would
otherwise trigger an unbounded object allocation; set it to zero only when the
caller provides an external input-size policy.

The cached page tree is now returned by reference internally. Page-oriented operations no longer copy the complete page-reference vector on every call; this matters for large documents and repeated extraction APIs.

Text, content-event, and image extraction now decode referenced content streams
into temporary chunks, calculate the final joined size, and reserve the output
buffer once before concatenation. This removes repeated capacity growth for pages
with many `/Contents` entries while preserving the existing extraction API.

The internal stream parser also exposes a `std::string_view` path for locating
stream bytes. Filter decoding consumes that view directly, so unfiltered stream
data is not copied once merely to build the filter input; the public string-returning
path remains available where ownership is required.

Decoded content streams are cached by indirect reference as well. The default
budget is 128 streams or 64 MiB; configure `maxCachedContentStreams` and
`maxCachedContentStreamBytes` for a different workload. Reuse can be inspected
with `GetContentStreamCacheHits()` and `GetContentStreamCacheMisses()`.

For PDFs around 1 GiB, `PdfDocument::Open(path)` automatically selects a
read-only mapping at the configured `memoryMapThresholdBytes` (256 MiB by
default); `OpenMapped()` remains available to force the policy. Mapping avoids a
second full-file resident copy; object, content, and
font caches then cap the materialized working set while complex drawing streams
are accessed. The writer's incremental editing path remains preferable when a
large source only needs localized changes.

The non-mapped file source now keeps one reusable random-access handle instead of
opening the file for every object read. Windows opens it with read/write/delete
sharing so temporary-file replacement and cleanup workflows remain compatible.

## Compressed object workloads

Repeated access to objects in the same `/ObjStm` reuses the decoded stream and its
parsed index. The index parser reads directly from `std::string_view` with
`std::from_chars`, avoiding the previous header substring and stream-parser copy.

```cpp
CPPPdf::PdfReaderOptions options;
options.limits.maxCachedObjectStreams = 64U;
options.limits.maxCachedObjectStreamBytes = 128U * 1024U * 1024U;
auto document = CPPPdf::PdfDocument::Open("compressed.pdf", options);
```

Set either limit to zero to disable decoded object-stream retention. Runtime
telemetry is available through `GetCachedObjectStreamCount()`,
`GetCachedObjectStreamBytes()`, `GetObjectStreamCacheHits()`, and
`GetObjectStreamCacheMisses()`. `ClearObjectCache()` clears both parsed objects and
decoded object streams.

Font resources referenced indirectly by multiple pages are parsed once per
document. The default limit is 256 resources and can be changed with
`PdfReaderLimits::maxCachedFontResources`; set it to zero to disable retention.
Use `GetCachedFontResourceCount()`, `GetFontResourceCacheHits()`, and
`GetFontResourceCacheMisses()` to inspect reuse.

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

Mapping remains opt-in. Mapped documents now retain the input source for the document lifetime and expose a contiguous read-only view directly to the parser, avoiding the previous full-file copy. Object bodies and decoded streams may still allocate when materialized.

## Streaming output sink

```cpp
std::ofstream output("report.pdf", std::ios::binary);
CPPPdf::PdfWriter writer;
// Add pages and content.
writer.Save(output);
```

The output sink is streamed directly to `std::ostream`; complex object bodies are still prepared before final serialization. Full object-by-object streaming and unchanged-stream passthrough remain future work.

## Security-kernel hot path

AES/RC4 encryption is applied object by object, so the complete rewritten document is
not retained twice in memory. The stream transformer updates `/Length` with a linear
token scan instead of compiling a regular expression for every stream, reserves the
final object buffer once, and reuses the operating-system random source per thread for
AES initialization vectors. Password rewrites deserialize and emit one indirect object
at a time; xref offsets are the only document-wide write state.

Incremental page, annotation, and AcroForm updates now share one block-copying writer.
It copies input in 64 KiB chunks, retains only revised objects and xref offsets,
preserves the encryption file ID, and applies the existing object key to each encrypted
revision object.

## Benchmark discipline

Use the cross-engine validation tools under `tools/validation/` for repeatable open, extraction, rendering and feature measurements. Compare medians on the same machine, build type, corpus and thread count.

## Remaining gaps to leading engines

Pdf++ 0.40 does not claim parity with MuPDF or PDFium. The largest remaining opportunities are:

- Lazy indirect-object parsing over the mapped view; object streams are now decoded once and cached, but object materialization still returns owned strings.
- Cross-document shared font/CMap cache and arena allocation remain future work; each document now retains its own bounded parsed font/CMap resources.
- Independent per-worker resolver contexts for parallel preload.
- Optional RE2 or PCRE2 JIT backend.
- Arena allocation and PDF-name interning.
- True object-by-object writer streaming and unchanged compressed-stream passthrough.
