# Pdf++ fuzzing

The optional fuzz targets use Clang libFuzzer with AddressSanitizer and
UndefinedBehaviorSanitizer. The core must stay dependency-free and portable, so
the fuzzers are not part of the default Visual Studio solution; they are built
with CMake under Clang.

## Targets

| Target | Input | Exercises |
|---|---|---|
| `PdfPP.FuzzReader` | PDF bytes | xref/object/stream parsing, page tree, object streams, bounded reader limits |
| `PdfPP.FuzzContent` | content stream | content-tokenizer and graphics/text operator parsing |
| `PdfPP.FuzzFilter` | encoded stream | Flate, ASCIIHex, ASCII85, RunLength, LZW decoders |
| `PdfPP.FuzzCffFont` | CFF program | charset/CharStrings/Private DICT parsing and Type 2 charstring interpreter |
| `PdfPP.FuzzTrueTypeFont` | TrueType program | table parsing, glyph outlines, and advance-width cache |

## Configure and build

```text
cmake -S . -B build-fuzz -DPDFPP_BUILD_FUZZERS=ON -DPDFPP_BUILD_TESTS=OFF
cmake --build build-fuzz --config Release
```

## Run

Run each target with a seed corpus:

```text
PdfPP.FuzzReader.exe corpus\pdf -artifact_prefix=artifacts\fuzz\ -max_len=1048576
PdfPP.FuzzContent.exe corpus\content -artifact_prefix=artifacts\fuzz\
PdfPP.FuzzFilter.exe corpus\streams -artifact_prefix=artifacts\fuzz\
PdfPP.FuzzCffFont.exe corpus\fonts -artifact_prefix=artifacts\fuzz\
PdfPP.FuzzTrueTypeFont.exe corpus\fonts -artifact_prefix=artifacts\fuzz\
```

## Policy

The harnesses accept normal parser exceptions. Sanitizer findings, non-termination
and unexpected process failures are actionable bugs. Every input size is bounded by
the reader limits or by the parser's own allocation guards, so the harnesses must
terminate promptly on malformed data.
