# Cross-engine PDF validation

`compare_engines.py` generates a deterministic ReportLab corpus and compares:

- page count and text markers from Pdf++;
- page count and text markers from pypdf;
- page count from Poppler/pdfinfo;
- a first-page PNG render from Poppler.

Build `PdfPP.Inspect` with `PDFPP_BUILD_VALIDATION_TOOLS=ON`, then run:

```text
python tools/validation/compare_engines.py --pdfpp path/to/PdfPP.Inspect.exe
```

The work directory defaults to `tmp/pdfs/validation`. Add qpdf or MuPDF adapters here when those engines are available in CI.

MuPDF validation uses PyMuPDF, which bundles the MuPDF engine:

```text
python tools/validation/compare_mupdf.py input.pdf --render tmp/pdfs/validation/mupdf-page-1.png
```

Install it in an isolated environment with `python -m pip install PyMuPDF`. Check the package license before distributing it with a product; MuPDF open-source licensing is AGPL/commercial.

## Differential rendering

`compare_render.py` renders one page with both Pdf++ and MuPDF and compares
output dimensions and dark-pixel coverage:

```text
python tools/validation/compare_render.py input.pdf \
  --pdfpp path/to/PdfPP.Inspect.exe --dpi 72 --page 2
```

- `--page` selects a 1-based page (default 1).
- `--dpi` sets the render resolution (default 72).
- `--coverage-tolerance` is the allowed dark-pixel coverage delta (default 0.20).

Dimension mismatches are hard failures (exit 1). Coverage differences beyond
tolerance print the JSON report and exit 2 with a warning, so text-shaping and
font-rasterization differences between engines do not fail the whole run. MuPDF
rendering prefers the `mutool` CLI (`mupdf-tools`) and falls back to PyMuPDF.

## Differential text extraction

`compare_text.py` compares per-page extracted text between Pdf++ and MuPDF using
token overlap:

```text
python tools/validation/compare_text.py input.pdf --pdfpp path/to/PdfPP.Inspect.exe
```

- `--overlap-threshold` sets the minimum per-page token overlap (default 0.10).
- Page-count mismatches are hard failures; pages below the threshold are listed
  and the run exits 2 so complex-layout or image-only pages do not fail CI.

## Corpus generation

`PdfPP.GenCorpus` writes deterministic, self-owned fixtures into
`tests/corpus/generated/` covering text, vector paths, images, transparency, and
multi-page documents:

```text
PdfPP.GenCorpus.exe tests/corpus/generated
```

Each fixture can then be validated with the render and text scripts above.

## Feature/performance matrix

```text
python tools/validation/feature_benchmark_matrix.py \
  --pdfpp path/to/PdfPP.Inspect.exe \
  --pdfinfo path/to/pdfinfo.exe \
  --iterations 5
```
