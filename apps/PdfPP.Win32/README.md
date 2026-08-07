# PdfPP.Win32

Native Win32 GUI reader for Pdf++.

## Architecture

`PdfPP.Win32` links directly to the public C++ API in `Pdf++.Core`.
`PdfPP.Native` is not required, linked, loaded, or copied beside the GUI
executable. It can remain in the solution as an optional C ABI project for
future C#/Python/external consumers.

```text
PdfPP.Win32 -> Pdf++.Core
             -> PdfPP.ModernWin32
             -> Windows.Data.Pdf/GDI+ renderer adapter
```

## Features

- Opens PDF files directly through `CPPPdf::PdfDocument`.
- Renders pages on a background worker thread.
- Previous/next page navigation and direct page entry.
- Zoom in/out, actual size, fit page, and fit width.
- Continuous page scrolling with per-monitor DPI awareness.
- PDF outline/bookmark tree.
- Optional page shadow.
- File title, page count, zoom, and document properties.
- Accepts a PDF path on the command line.
- Merge, extract, split, delete, duplicate, move, reorder, and reverse pages.
- AES-256 password add/remove/change operations.

## Visual Studio

1. Open `Pdf++.sln`.
2. Select `x64 | Debug` or `x64 | Release`.
3. Set `PdfPP.Win32` as the startup project.
4. Build and run.

The GUI now builds from these direct dependencies:

```text
zlibstatic
Pdf++.Core
PdfPP.ModernWin32
PdfPP.Win32
```

`PdfPP.Native` does not need to be built before launching the GUI.

The executable is written to:

```text
build\x64\<Configuration>\PdfPP.Win32.exe
```
