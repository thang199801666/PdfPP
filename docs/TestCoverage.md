# Pdf++ Unit Test Coverage

This document maps the current public feature set to its automated unit or integration coverage.

| Feature area | Test group |
|---|---|
| Public types and version | `API.PublicArchitecture` |
| PDF object model | `API.ObjectModel` |
| File, memory, stream and custom input | `API.InputSources`, `API.DocumentOpenOverloads` |
| Object parser, xref and page tree | `Core.ParserFiltersFontsContentText`, `Reader.Integration` |
| Filters and predictors | `API.Filters`, `Core.ParserFiltersFontsContentText` |
| Font resources, CMap and TrueType subset | `Core.ParserFiltersFontsContentText`, `API.UnicodeTrueTypeWriting` |
| Text extraction | `Core.ParserFiltersFontsContentText`, `API.DocumentOpenOverloads` |
| Literal and regex text search | `API.TextSearch`, `Feature.RegexSearchOptionsAndGeometry` |
| Canvas text and graphics operators | `API.ContentCommands`, `Feature.CanvasGraphicsStateAndPaths`, `Feature.CanvasTextValidation` |
| Images | `API.Images`, `Feature.TextImageStampsAndWatermarks` |
| Document metadata | `API.WriterDocumentInfo` |
| Page creation, insertion, movement and removal | `API.WriterValidation`, `Feature.PageMutationRemapsDependentFeatures` |
| Page editing and content layers | `API.PageEditingAndOrganization` |
| Merge, split and page import | `API.PageImport`, `Writer.PageEditingFormsIntegration` |
| Bookmarks/outlines | `API.WriterBookmarks`, `Feature.BookmarkValidationAndLifecycle` |
| Named destinations and links | `Feature.NamedDestinationsAndLinks` |
| Viewer and print preferences | `Feature.ViewerPreferencesSerialization`, `Feature.ViewerPreferencesValidation` |
| Page labels | `Feature.PageLabelsLifecycleAndRemapping` |
| Open action | `Feature.OpenActionLifecycleAndRemapping` |
| Text/image stamps and watermarks | `Feature.TextImageStampsAndWatermarks` |
| Embedded files and attachment annotations | `API.WriterEmbeddedFiles`, `Feature.EmbeddedFileLifecycleAndValidation` |
| Annotations and keyword highlighting | `API.AnnotationsAndHighlight` |
| AcroForm operations and flattening | `Writer.PageEditingFormsIntegration` |
| Save and read-back validation | `Feature.SaveValidationAndRoundTrip` |

The executable currently reports 29 named subtests across the core, reader, writer, public API and feature suites. Strict builds compile the same suite with warnings treated as errors.

| Rendering bitmap/path/text foundation | `Feature.RenderingFoundation` | Creates a PDF, renders it, validates dimensions and pixels, exports PPM, and checks invalid render limits |
