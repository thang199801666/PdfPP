# Pdf++ project structure

`src/Pdf++.Core/include/CPPPdf` is the canonical supported consumer API. `src/Pdf++.Core/src/Internal` is never installed and may change without compatibility guarantees.

- `Core`: common value types and ABI-neutral primitives.
- `IO`: reader options, limits, and input abstractions.
- `Objects`: typed PDF object model.
- `Document`: document and page-facing API.
- `Filters`: public stream-decoding pipeline.
- `src/Parsing`: parser implementations.
- `src/Internal`: private parser contracts and implementation details.

New code should include `<CPPPdf/CPPPdf.h>` or canonical component headers. `CPPPdf.hpp` remains a forwarding wrapper for source compatibility, as do root-level `PdfDocument.hpp`, `PdfPage.hpp`, and `PdfReader.hpp`.

CMake consumers use:

```cmake
find_package(PdfPP CONFIG REQUIRED)
target_link_libraries(app PRIVATE PdfPP::Core)
```
