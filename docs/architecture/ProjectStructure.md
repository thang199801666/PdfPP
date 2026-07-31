# Pdf++ project structure

`include/CPPPdf` is the supported consumer API. `src/Internal` is never installed and may change without compatibility guarantees.

- `Core`: common value types and ABI-neutral primitives.
- `IO`: reader options, limits, and input abstractions.
- `Objects`: typed PDF object model.
- `Document`: document and page-facing API.
- `Filters`: public stream-decoding pipeline.
- `src/Parsing`: parser implementations.
- `src/Internal`: private parser contracts and implementation details.

New code should include `<CPPPdf/CPPPdf.hpp>` or canonical component headers. Root-level `PdfDocument.hpp`, `PdfPage.hpp`, and `PdfReader.hpp` remain forwarding headers for source compatibility.

CMake consumers use:

```cmake
find_package(PdfPP CONFIG REQUIRED)
target_link_libraries(app PRIVATE CPPPdf::Core)
```
