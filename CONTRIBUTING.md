# Contributing to Pdf++

Thank you for helping improve Pdf++.

## Before opening a pull request

1. Search existing issues and pull requests.
2. Keep changes focused and avoid unrelated formatting churn.
3. Add or update tests for every behavior change.
4. Update `CHANGELOG.md` and relevant documentation when public behavior changes.
5. Do not add PDF files, fonts, images, or other assets unless their redistribution license is documented.

## Build and test

```bash
cmake -S . -B build \
  -DPDFPP_BUILD_TESTS=ON \
  -DPDFPP_BUILD_SAMPLES=ON \
  -DPDFPP_BUILD_BENCHMARKS=OFF \
  -DPDFPP_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Linux, sanitizer validation is recommended:

```bash
cmake -S . -B build-asan \
  -DPDFPP_BUILD_TESTS=ON \
  -DPDFPP_BUILD_SAMPLES=OFF \
  -DPDFPP_BUILD_BENCHMARKS=OFF \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Coding guidelines

- Use C++20.
- Prefer RAII and value semantics.
- Avoid raw owning pointers.
- Keep parser limits and malformed-input behavior explicit.
- Avoid allocations and copies in reader, renderer, extraction, and serialization hot paths.
- Use braces for all control-flow blocks.
- Keep the core warning-clean under MSVC `/W4 /WX` and GCC/Clang `-Wall -Wextra -Wpedantic -Werror`.

## Tests

Tests live under `tests/Unit`. Add a focused test for each new feature, edge case, validation branch, or regression. The public feature-to-test mapping is maintained in `docs/TestCoverage.md`.

## Commit and pull-request scope

Use clear imperative commit messages, for example:

```text
Add clipping-state restoration tests
Optimize CMap lookup cache
Fix xref-stream recovery bounds
```

By submitting a contribution, you agree that it is licensed under the
repository's GNU General Public License v3.0.
