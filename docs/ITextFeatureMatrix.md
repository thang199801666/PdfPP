# iText Feature Matrix

This matrix tracks an independent, public-domain-compatible implementation plan.
It describes capabilities, not copied iText code or API names.

## Core PDF

| Feature | Pdf++ status | Priority |
|---|---|---|
| PDF 1.x object model | Partial | High |
| PDF 1.7 / PDF 2.0 features | Partial | High |
| Read and write indirect objects | Implemented | - |
| Classic xref tables | Implemented | - |
| Xref streams and object streams | Partial | High |
| Incremental updates | Partial | High |
| Stream filters | Partial | High |
| Encryption and permissions | Partial | High |
| Document metadata and viewer preferences | Partial | Medium |
| Page tree, boxes, rotation and labels | Partial | High |
| Merge, split, copy and reorder pages | Partial | High |
| Attachments and portfolios | Partial | Medium |

## Content Creation

| Feature | Pdf++ status | Priority |
|---|---|---|
| Low-level content streams | Implemented | - |
| Canvas paths and graphics state | Partial | High |
| Text operators and Unicode text | Partial | High |
| Embedded TrueType fonts | Partial | High |
| Type1, CFF and OpenType fonts | Embedded program detection and diagnostics; shaping/rasterization incomplete | High |
| Images and masks | Partial; DeviceGray/RGB/CMYK and Indexed rendering implemented | High |
| Patterns and shadings | Axial/radial shading, Type 0/2/3/4 function foundations; mesh/pattern integration remains | High |
| Transparency groups and blend modes | Blend modes partial; groups missing | High |
| Annotations and links | Partial | High |
| AcroForm creation/fill/flatten | Partial | High |
| XFA | Missing | Low |
| Optional content layers | Missing | Medium |

## Layout

| Feature | Pdf++ status | Priority |
|---|---|---|
| Paragraph layout | Missing | High |
| Font fallback and shaping | Partial | High |
| Tables | Missing | High |
| Lists and tabs | Missing | Medium |
| Headers, footers and page flow | Missing | Medium |
| Images in layout | Missing | Medium |
| SVG content | Missing | Medium |
| HTML/CSS conversion | Missing; separate add-on scope | Low |

## Extraction and Rendering

| Feature | Pdf++ status | Priority |
|---|---|---|
| Text extraction and search | Partial | High |
| Geometry-aware extraction | Implemented | - |
| Display list replay | Implemented foundation; renderer path replayed | High |
| Path clipping and fill rules | Partial | High |
| Font glyph rasterization | Partial | High |
| Image interpolation | Partial | Medium |
| Color management / ICC | Missing | High |
| Soft masks and blend modes | Missing | High |
| Tagged structure extraction | Missing | Medium |
| PDF rendering parity corpus | Missing | High |

## Standards and Security

| Feature | Pdf++ status | Priority |
|---|---|---|
| PDF/A validation and creation | Missing | High |
| PDF/UA tagging and validation | Missing | Medium |
| Digital signatures | Missing | High |
| PAdES workflows | Missing | Medium |
| Long-term validation data | Missing | Low |
| XFDF import/export | Missing | Medium |
| Redaction | Missing | High |
| OCR integration | Missing; separate add-on scope | Low |

## Implementation Order

1. Complete display-list replay and graphics-state fidelity.
2. Complete fonts, shaping, colors, patterns, masks and blend modes.
3. Complete forms, annotations, links, redaction and document manipulation.
4. Add layout primitives: paragraphs, tables, lists and page flow.
5. Add standards, signatures, accessibility and validation.
6. Add optional integrations such as HTML, SVG and OCR.
