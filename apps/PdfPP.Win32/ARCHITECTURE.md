# PdfPP.Win32 architecture

The Win32 reader is organized as a thin presentation application over reusable native libraries.

```text
Main
  -> ReaderApplication
       -> PageCache
       -> NativePdfDocument
            -> PdfPP.Native
                 -> Pdf++.Core
       -> PdfPP.ModernWin32
```

## Module boundaries

### Main

Owns only the Windows process entry point. It must not contain document, rendering, or UI state.

### ReaderApplication

Owns window handles and coordinates commands, input, DPI changes, asynchronous loading, rendering and painting. It may depend on all application-level modules, but reusable modules must not depend on it.

### NativePdfDocument

Is the ownership boundary for the native PDF handle. It converts renderer output into an owned BGRA `PageBitmap`, so UI code never manages `pdfpp_free`, raw document pointers, or pixel-channel conversion.

### PageCache

Owns the bounded most-recently-used page bitmap collection. Cache identity includes page, semantic zoom and monitor DPI. It has no Win32 window dependency and can be unit tested independently.

### PdfPP.ModernWin32

Contains reusable native control styling, DPI-aware fonts and theme primitives. It must remain independent from the reader application.

## Dependency rules

1. Native resource ownership stays behind RAII types.
2. Worker threads return owned values; they do not mutate HWND state.
3. Only the UI thread sends messages or changes controls.
4. DPI is part of render and cache identity.
5. New features receive their own module before adding state to `ReaderApplication`.

## Planned extraction seams

As features grow, split `ReaderApplication` in this order:

1. `RenderCoordinator`: cancellation, render queue and prefetch policy.
2. `DocumentController`: open/close, page navigation and search state.
3. `ReaderView`: HWND creation, layout, painting and input translation.
4. `CommandRouter`: menus, accelerators and enable/checked state.

This order keeps behavior stable while progressively removing the remaining window-level global state.
