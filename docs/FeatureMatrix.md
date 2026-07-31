# Pdf++ feature matrix

Legend: **Yes** = supported, **Partial** = supported with documented limitations, **No** = not implemented.

| Feature | Read | Create | Modify | Notes |
|---|---:|---:|---:|---|
| Classic xref table | Yes | Yes | Yes | Core workflow |
| Xref stream | Yes | Partial | Partial | Reader support is more mature than writer coverage |
| Object stream | Yes | Partial | Partial | Compressed-object reading supported |
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
| Encryption | Detection | No | No | Security handlers not implemented |
| Digital signatures | No | No | No | Planned future subsystem |
| PDF/A and PDF/UA | No | No | No | Compliance validation not implemented |
| High-level layout | N/A | No | No | Paragraph/table pagination is not implemented |

## Validation guidance

For production pipelines, validate generated files with an independent PDF implementation such as qpdf, PDFium, MuPDF, or Poppler. Use representative real-world PDF corpora before enabling untrusted uploads.

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
| Image XObject rendering | No | Planned next |
| Exact embedded-font glyph rendering | No | Planned |
| Clipping, transparency and blend modes | No | Planned |
| Patterns, shadings and advanced color spaces | No | Planned |

| CPU page rendering | Yes (paths, colors, transforms, fallback text, raw decoded images, supersampling AA) |
