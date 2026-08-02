# PdfPP.Win32 architecture

The Win32 reader is organized as a thin presentation application over reusable native libraries.

```text
Main
  -> ReaderApplication (entry point, message pump)
       -> ReaderView      (window/canvas/tab procs, layout, painting, fullscreen)
       -> ReaderDocument  (open/close, navigation, search, print, bookmarks)
       -> ReaderRendering (geometry, render/cache, prefetch, scrolling, zoom)
       -> ReaderTabs      (tab strip painting, tab lifecycle, close button)
       -> ReaderSettings  (recent files, favorites, menus, fonts, about)
       -> ReaderUtils     (generic helpers: DPI, UTF-8, drawing, clipboard)
       -> AppSettings
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

Owns the process entry, window creation and the message loop. It wires the modules together but contains no document/rendering logic itself.

### ReaderView

Owns the window procedures (`windowProc`, `canvasProc`, `tabBarProc`), ribbon/sidebar layout, canvas painting and fullscreen toggling. It only dispatches commands; it does not own document or render state.

### ReaderDocument

Owns document open/close, tab navigation semantics, text search, printing and the bookmark tree.

### ReaderRendering

Owns page geometry (absolute offsets used to size the scrollbar to the whole document), the render thread/cache, prefetch policy and all scrolling/zoom math.

### ReaderTabs

Owns tab strip painting (custom, Chrome/Photoshop style), hit-testing and the tab lifecycle (add, switch, close with confirmation).

### ReaderSettings

Owns recent files, favorites, menu construction, DPI-aware fonts, and about/properties dialogs.

### ReaderUtils

Holds dependency-free helpers (DPI scaling, UTF-8 conversion, overlay painting, gradient fill, clipboard) used across modules.

### AppSettings

Owns the settings file (zoom, page layout, sidebar visibility, recent files, favorites) persisted as a UTF-8 text file next to the executable. It has no Win32 window dependency and can be unit tested independently.

### NativePdfDocument

Is the ownership boundary for the native PDF handle. It converts renderer output into an owned BGRA `PageBitmap`, so UI code never manages `pdfpp_free`, raw document pointers, or pixel-channel conversion.

### PageCache

Owns the bounded most-recently-used page bitmap collection. Cache identity includes page, semantic zoom and monitor DPI. It has no Win32 window dependency and can be unit tested independently.

### PdfPP.ModernWin32

Contains reusable native control styling, DPI-aware fonts and theme primitives. It must remain independent from the reader application.

## Shared state

All reader modules share one set of application-global variables declared in
`ReaderState.hpp` (C++17 `inline` variables). This mirrors the original
monolithic file; the split is purely organizational. The `RenderResult` and
`OpenResult` worker-thread results are transferred under mutexes and only
consumed on the UI thread.

## Dependency rules

1. Native resource ownership stays behind RAII types.
2. Worker threads return owned values; they do not mutate HWND state.
3. Only the UI thread sends messages or changes controls.
4. DPI is part of render and cache identity.
5. New features receive their own module before adding state to `ReaderState`.
