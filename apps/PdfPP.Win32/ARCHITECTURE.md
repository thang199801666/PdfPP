# PdfPP.Win32 architecture

The Win32 reader links directly to the C++ core library. `PdfPP.Native` is not
on the GUI execution path and is reserved for external C ABI bindings.

```text
Main
  -> ReaderApplication (entry point, message pump)
       -> ReaderView      (window/canvas/tab procs, layout, painting, fullscreen)
       -> ReaderDocument  (open/close, navigation, search, print, bookmarks)
       -> ReaderRendering (geometry, render/cache, prefetch, scrolling, zoom)
       -> ReaderTabs      (tab strip painting, tab lifecycle, close button)
       -> ReaderSettings  (recent files, favorites, menus, fonts, about)
       -> ReaderTools     (merge/split/page organization/password workflows)
       -> ReaderUtils     (generic helpers: DPI, UTF-8, drawing, clipboard)
       -> AppSettings
       -> PageCache
       -> ReaderPdfDocument
            -> Pdf++.Core (direct C++ API)
            -> Windows.Data.Pdf/GDI+ (app-local rasterization adapter)
       -> PdfPP.ModernWin32

PdfPP.Native
  -> Pdf++.Core
  -> optional C ABI for external consumers only
```

## Module boundaries

### Main

Owns only the Windows process entry point. It must not contain document,
rendering, or UI state.

### ReaderApplication

Owns process initialization, window creation, and the message loop. It wires
the modules together but contains no document/rendering logic itself.

### ReaderView

Owns the window procedures (`windowProc`, `canvasProc`, `tabBarProc`),
ribbon/sidebar layout, canvas painting, and fullscreen handling. It dispatches
commands but does not parse or modify PDF files.

### ReaderDocument

Owns document open/close, navigation semantics, text search, printing, and the
bookmark tree.

### ReaderRendering

Owns page geometry, the render thread/cache, prefetch policy, and all
scrolling/zoom math.

### ReaderTabs

Owns tab strip painting, hit testing, and the tab lifecycle.

### ReaderSettings

Owns recent files, favorites, menu construction, DPI-aware fonts, and the
about/properties dialogs.

### ReaderTools

Owns Win32 dialogs and workflows for merge, split, extraction, page
organization, and passwords. The actual PDF work is invoked through the direct
C++ methods exposed by `ReaderPdfDocument` and implemented by `Pdf++.Core`.

### ReaderUtils

Holds dependency-light helpers used across reader modules.

### AppSettings

Owns the UTF-8 settings file next to the executable. It has no document or HWND
ownership.

### ReaderPdfDocument

Owns `CPPPdf::PdfDocument` directly through RAII. It provides the application
model used by the UI, converts `PdfBitmap` RGBA data to Win32 BGRA, and contains
the Windows rasterization fallback. It does not call C ABI functions, does not
own opaque native handles, and does not load `PdfPP.Native.dll`.

### PageCache

Owns the bounded most-recently-used page bitmap collection. Cache identity
includes page, semantic zoom, and monitor DPI.

### PdfPP.ModernWin32

Contains reusable control styling, DPI-aware fonts, and theme primitives. It
remains independent from PDF parsing and editing.

## Shared state

Reader modules share application-global variables declared in
`ReaderState.hpp`. Worker-thread results are transferred under mutexes and are
consumed only on the UI thread.

## Dependency rules

1. `PdfPP.Win32` may call the public C++ API of `Pdf++.Core` directly.
2. `PdfPP.Win32` must not call `pdfpp_*` C ABI functions or link
   `PdfPP.Native.lib`.
3. `PdfPP.Native` remains optional and is intended only for external bindings.
4. Worker threads return owned values; they do not mutate HWND state.
5. Only the UI thread sends messages or changes controls.
6. DPI is part of render and cache identity.
7. New user-facing workflows receive their own reader module.
