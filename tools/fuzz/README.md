# Pdf++ fuzzing

The optional `PdfPP.FuzzReader` target uses Clang libFuzzer and AddressSanitizer/UndefinedBehaviorSanitizer.

Configure with:

```text
cmake -S . -B build-fuzz -DPDFPP_BUILD_FUZZERS=ON -DPDFPP_BUILD_TESTS=OFF
cmake --build build-fuzz --config Release
```

Run the target with a seed corpus:

```text
PdfPP.FuzzReader.exe corpus\pdf -artifact_prefix=artifacts\fuzz\ -max_len=1048576
```

The harness uses bounded reader limits. It accepts normal parser exceptions; sanitizer findings, non-termination and unexpected process failures are actionable bugs.
