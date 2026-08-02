# Pdf++ feature matrix

Legend: **Yes** = supported, **Partial** = supported with documented limitations, **No** = not implemented.

| Feature | Read | Create | Modify | Notes |
|---|---:|---:|---:|---|
| Classic xref table | Yes | Yes | Yes | Core workflow |
| Xref stream | Yes | Partial | Partial | Reader support is more mature than writer coverage |
| Object stream | Yes | Partial | Partial | Compressed-object reading uses a bounded decoded-stream LRU cache |
| Page tree | Yes | Yes | Yes | Import, reorder, remove, split, merge |
| Metadata document info | Yes | Yes | Yes | Standard Info dictionary fields |
| Hierarchical bookmarks | Yes | Yes | Yes | Page remapping supported |
| Text extraction/search | Partial | N/A | N/A | Literal and ECMAScript regex search with geometry; complex scripts require more coverage |
| Canvas graphics | N/A | Yes | Yes | Paths, transforms, clipping, color, opacity |
| Base-14 fonts | Yes | Yes | Yes | Standard PDF fonts |
| TrueType fonts | Partial | Yes | Yes | UTF-8 mapping and subsetting; no full shaping engine |
| Images | Partial | Yes | Yes | Common RGB, grayscale, and JPEG workflows |
| Annotations | Partial | Yes | Yes | Core annotation/highlight APIs |
| AcroForm | Partial | Partial | Partial | Basic field update, appearance, and flattening |
| Encryption | Yes | Yes | Partial | AES-128 (R4) and RC4-128 (R3); password/permission-aware incremental page, annotation, and AcroForm updates plus change/remove password. Page organizer/import/highlighter and AES-256 (R6) remain unsupported |
| Digital signatures | No | No | No | Planned future subsystem |
| PDF/A and PDF/UA | No | No | No | Compliance validation not implemented |
| High-level layout | N/A | No | No | Paragraph/table pagination is not implemented |

## Validation guidance

For production pipelines, validate generated files with an independent PDF implementation such as qpdf, PDFium, MuPDF, or Poppler. Use representative real-world PDF corpora before enabling untrusted uploads.

## Untrusted-input safeguards

The reader applies configurable limits for indirect objects, recursion depth, decoded streams, object-stream entries, cached objects, cached object-stream count/bytes, cached font resources, cached content-stream count/bytes, and page count. Flate decoding used by xref and object streams is subject to the same decoded-stream limit as normal filter processing. Malformed input is rejected for truncated xref data, invalid or non-monotonic object-stream offsets, invalid stream boundaries, page-tree cycles, unsupported filters, and decompression-limit violations.

These safeguards reduce resource-exhaustion risk but are not a substitute for process isolation when handling hostile PDFs.

| Named destinations | N/A | Supported | Supported during writer page reordering |
| Link annotations | Partial read through object API | URI and named-destination links | Writer API |

| Viewer preferences and page layout | N/A | Yes | Writer API |
| Page labels | N/A | Yes | Number tree with decimal, Roman, alphabetic, prefix, and start number |

| Open action and print preferences | N/A | Supported | Page destination, scaling, duplex, tray selection, copies |

| Embedded files and file attachments | Partial | Yes | Writer API | EmbeddedFiles name tree, associated files, and page annotations |

## Rendering

| Capability | Status | Notes |
|---|---:|---|
| RGBA bitmap raster target | Yes | CPU-backed `PdfBitmap` |
| Page box, DPI and rotation mapping | Yes | MediaBox/CropBox and 0/90/180/270 rotation |
| Vector path stroke/fill | Initial | Lines, rectangles and flattened cubic Beziers |
| RGB and grayscale paint state | Initial | Stroke/fill colors and line width |
| Text rasterization | Initial | ASCII fallback from extracted text geometry |
| Image XObject rendering | Partial | Raw decoded DeviceGray/RGB/CMYK and inline images; optional JPEG/JPX codecs remain planned |
| Exact embedded-font glyph rendering | No | Planned |
| Clipping, transparency and blend modes | No | Planned |
| Patterns, shadings and advanced color spaces | No | Planned |

| CPU page rendering | Yes (paths, colors, transforms, fallback text, raw decoded images, supersampling AA) |
