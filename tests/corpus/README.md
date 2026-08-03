# Pdf++ test corpus

The corpus contains files used by reader regression tests, rendering fixtures, and
the cross-engine validation tools.

## License policy

Every file in this directory must be redistributable with the Pdf++ repository:

- **Self-generated files** (produced by `tools/validation/compare_engines.py`,
  `PdfPP.WriteValidation`, or unit-test fixtures) are owned by the Pdf++ project
  and released under the repository `LICENSE`.
- **Third-party files** must carry an explicit note below naming the origin and a
  license that permits redistribution (public domain, MIT, BSD, CC0, or an
  equivalent permissive license). Never add proprietary or license-unclear PDFs.

If you add a file, add a bullet in the appropriate section below.

## Malformed / fuzz seeds

These files are intentionally invalid seed inputs for reader regression tests and
libFuzzer. They must be rejected without a crash or unbounded allocation; they are
not expected to render.

- `truncated.pdf`: header without xref/trailer
- `bad-xref-offset.pdf`: valid-looking header with an invalid `startxref`
- `unterminated-object.pdf`: object with an unterminated dictionary

## Valid fixtures

- `document.pdf`: a 592-page technical book sample used by rendering and
  differential-validation scripts. Self-generated fixture; redistribution permitted
  under the repository `LICENSE`.

## Generated corpus

`PdfPP.GenCorpus` (built with `PDFPP_BUILD_VALIDATION_TOOLS=ON`) writes a
deterministic set of fixtures into `tests/corpus/generated/` covering text,
vector paths, images, transparency, and multi-page documents. These are produced
by Pdf++'s own writer, so they are redistributable under the repository `LICENSE`
and are regenerated on demand rather than committed:

```text
PdfPP.GenCorpus.exe tests/corpus/generated
```

Validate them against MuPDF after regeneration:

```text
python tools/validation/compare_render.py tests/corpus/generated/corpus-vector.pdf \
  --pdfpp path/to/PdfPP.Inspect.exe --dpi 72 --page 1
python tools/validation/compare_text.py tests/corpus/generated/corpus-multipage.pdf \
  --pdfpp path/to/PdfPP.Inspect.exe
```
