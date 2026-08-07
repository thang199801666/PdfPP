# Build and project layout

The canonical Pdf++ Core source tree is:

```text
src/Pdf++.Core/
├── include/CPPPdf/
├── src/
├── Pdf++.Core.vcxproj
└── Pdf++.Core.vcxproj.filters
```

Do not copy a second Core project or public-header tree to the repository root.

## Visual Studio

Open `Pdf++.sln`, select `Debug|x64` or `Release|x64`, then build the solution. Both Core configurations are static libraries because the C++ public API does not expose DLL import/export decoration. The native bridge is the DLL boundary.

## CMake

```bash
cmake -S . -B build -DPDFPP_BUILD_TESTS=ON -DPDFPP_BUILD_UNIT_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
