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

For the broader feature/performance matrix:

```text
python tools/validation/feature_benchmark_matrix.py \
  --pdfpp path/to/PdfPP.Inspect.exe \
  --pdfinfo path/to/pdfinfo.exe \
  --iterations 5
```
