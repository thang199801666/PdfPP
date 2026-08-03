# Pdf++ Unit Test Coverage

This document maps the current public feature set to its automated unit or integration coverage.

| Feature area | Test group |
|---|---|
| Public types and version | `API.PublicArchitecture` |
| PDF object model | `API.ObjectModel`, `Object.Parser` |
| File, memory, stream and custom input | `API.InputSources`, `API.DocumentOpenOverloads`, `IO.InputSources` |
| Object parser, xref and page tree | `Object.Parser`, `Reader.MinimalPdfParsing`, `Reader.XrefRecovery` |
| Filters and predictors | `API.Filters`, `Filters.Decoders` |
| Font resources, CMap and TrueType subset | `Fonts.CMapsAndResources`, `API.UnicodeTrueTypeWriting`, `Fonts.CffParser` |
| Text extraction | `Text.Extractor`, `Reader.FontExtraction` |
| Literal and regex text search | `Text.Search`, `Text.SearchOptions`, `Feature.RegexSearchOptionsAndGeometry` |
| Canvas text and graphics operators | `API.ContentCommands`, `Content.Processor`, `Feature.CanvasGraphicsStateAndPaths`, `Feature.CanvasTextValidation` |
| Images | `API.Images`, `Reader.InlineImageExtraction`, `Reader.SoftMaskImageExtraction`, `Feature.TextImageStampsAndWatermarks` |
| Document metadata | `API.WriterDocumentInfo` |
| Page creation, insertion, movement and removal | `API.WriterValidation`, `Feature.PageMutationRemapsDependentFeatures` |
| Page editing and content layers | `API.PageEditingAndOrganization`, `Reader.PageEditor` |
| Merge, split and page import | `API.PageImport`, `Writer.CanvasCatalogAndPageOrganizer` |
| Bookmarks/outlines | `API.WriterBookmarks`, `Feature.BookmarkValidationAndLifecycle` |
| Named destinations and links | `Feature.NamedDestinationsAndLinks` |
| Viewer and print preferences | `Feature.ViewerPreferencesSerialization`, `Feature.ViewerPreferencesValidation` |
| Page labels | `Feature.PageLabelsLifecycleAndRemapping` |
| Open action | `Feature.OpenActionLifecycleAndRemapping` |
| Text/image stamps and watermarks | `Feature.TextImageStampsAndWatermarks` |
| Embedded files and attachment annotations | `API.WriterEmbeddedFiles`, `Feature.EmbeddedFileLifecycleAndValidation` |
| Annotations and keyword highlighting | `API.AnnotationsAndHighlight`, `Reader.AnnotationEditor` |
| Advanced annotations (FreeText/Ink/Polygon/Polyline/Square/Circle/Stamp) | `API.AdvancedAnnotationsAndXfdf` | Add, remove (filtered and full), update contents, appearance generation, XFDF export/import round trip, flattening (full + filtered), reply threads (`/IRT`/`/RT`), and `/Popup` linkage |
| AcroForm operations and flattening | `Writer.CanvasCatalogAndPageOrganizer`, `Writer.PageEditingFormsIntegration` |
| Save and read-back validation | `Feature.SaveValidationAndRoundTrip`, `Writer.ObjectStreamRoundTrip`, `Writer.XrefStreamAndClassic` |
| Optional content (layers) | `Feature.OptionalContentLayers` | Register OCG groups, `/OC` BDC/EMC marking, `/OCProperties` catalog output, and missing-layer rejection |
| Incremental updates and resave | `Writer.IncrementalObjectStream`, `Writer.IncrementalEncryptedObjectStream`, `Writer.ResaveCollapsesIncremental`, `Writer.ResaveDeduplicatesStreams`, `Writer.ResaveEncryptedPreservesPasswords` |

The suite is organized by domain so a failure names the exact component. The
core runner reports 23 top-level cases, of which the reader suite covers 11
scenarios, the writer suite 9, the security suite 4, plus the API and feature
sub-suites. Strict builds compile the same suite with warnings treated as errors.

| Rendering bitmap/path/text foundation | `Rendering.BitmapBlendModes`, `Rendering.FunctionsAndShading`, `Feature.RenderingFoundation` | Blend modes, transparency-group compositing, PDF functions/shading, and page rendering with PPM export and invalid-limit checks |
| Transparency-group compositing | `Content.TransparencyGroupEvents`, `Feature.TransparencyGroupRendering`, `Feature.MarkedContentTransparencyGroupRendering` | Form XObject `/Group /S /Transparency` and BDC/EMC group discovery with blend-mode/alpha compositing |
| Embedded CFF font parsing and rendering | `Fonts.CffParser`, `Feature.EmbeddedCffFontRendering` | Hand-built CFF font, Type 2 charstring interpreter, and rasterized embedded glyph pixels |
| Digital-signature foundation | `Security.CryptoPrimitivesAndAlgorithms` | Prepare/sign/apply/inspect round trip with ByteRange digest-input validation |
| PDF/A conformance validation | `Validation.PdfAConforming`, `Validation.PdfAMissingMetadataAndOutput`, `Validation.PdfAPartMismatch`, `Validation.PdfUAStructure` | Conforming metadata/output-intent pass; missing metadata/output intent, part mismatch, and PDF/UA structure failures |
| Line dash patterns | `Content.DashPatternParsing`, `Feature.DashPatternRendering` | Content `d` parsing and pixel-level dash on/off alternation |
| Shading rendering + soft masks | `Feature.ShadingRenderingAndSoftMask` | Axial gradient render (blue→red) through a clip path, dictionary shading resource resolution, differing-size soft-mask sampling, and blended gradient compositing |
| Tiling patterns | `Feature.TilingPatternRendering` | `/Pattern cs` + `scn/SCN` fill with repeated tile content (BBox/XStep/YStep/Matrix), alternating color squares |
| Separation/DeviceN color spaces | `Feature.SeparationAndDeviceNRendering` | Tint-transform rendering into DeviceRGB alternate (blue→red), array-encoded color-space detection |
| Password and permission management | `Security.PasswordManagerLifecycle`, `Security.EncryptedPageEditing`, `Security.EncryptedForms` | Encrypt/change/remove passwords, permission-denied edits, and encrypted form updates |
| Differential rendering vs MuPDF | `tools/validation/compare_render.py` | `PdfPP.Inspect` render summary compared against `mutool`/PyMuPDF dimensions and dark-pixel coverage |
| Differential text vs MuPDF | `tools/validation/compare_text.py` | Per-page token overlap between Pdf++ and MuPDF extraction |
| Generated corpus | `PdfPP.GenCorpus` + `tests/corpus/generated/` | Deterministic text/vector/image/transparency/multi-page fixtures validated against MuPDF |
