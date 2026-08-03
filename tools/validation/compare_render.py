"""Differential render: compare Pdf++ output with MuPDF.

Renders page 1 of a PDF with PdfPP.Inspect (PPM) and with MuPDF, then compares
dimensions and dark-pixel coverage. Dimension mismatches are hard failures;
coverage differences beyond a tolerance are reported as warnings so text-shaping
and font-rasterization differences do not fail the whole run.

MuPDF rendering uses the bundled `mutool` CLI when available, otherwise falls back
to PyMuPDF (fitz). Install one of:

  sudo apt install mupdf-tools          # provides mutool
  python -m pip install PyMuPDF          # bundles the MuPDF engine (AGPL)

Check the MuPDF license before redistributing it with a product.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_render_line(executable: Path, pdf: Path, dpi: float, workdir: Path,
                      page: int) -> dict[str, object]:
    result = subprocess.run(
        [str(executable), str(pdf), "--render", str(workdir), "--dpi", str(dpi)],
        check=True, text=True, capture_output=True, encoding="utf-8", errors="replace")
    summary: dict[str, object] = {}
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if fields[0] == "pages":
            summary["pages"] = int(fields[1])
        elif fields[0] == "render" and int(fields[1]) == page - 1:
            summary["width"] = int(fields[2])
            summary["height"] = int(fields[3])
            summary["dark"] = int(fields[4])
    return summary


def read_ppm_dark(path: Path) -> tuple[int, int, int]:
    """Reads a P6 PPM header and counts dark (grey<128) samples."""
    with open(path, "rb") as handle:
        magic = handle.readline().strip()
        if magic != b"P6":
            raise RuntimeError(f"{path} is not a P6 PPM")
        header = []
        while len(header) < 3:
            line = handle.readline()
            line = line.strip()
            if line.startswith(b"#"):
                continue
            header.extend(line.split())
        width, height, maximum = int(header[0]), int(header[1]), int(header[2])
        if maximum != 255:
            raise RuntimeError(f"{path} has an unexpected max sample {maximum}")
        samples = handle.read(width * height * 3)
        dark = sum(1 for index in range(0, len(samples), 3) if samples[index] < 128)
    return width, height, dark


def mupdf_render_mutool(pdf: Path, dpi: float, workdir: Path, page: int) -> dict[str, object]:
    output = workdir / "mupdf-page-1.ppm"
    result = subprocess.run(
        ["mutool", "draw", "-o", str(output), "-r", str(int(dpi)), "-F", "ppm",
         "-p", str(page), str(pdf)],
        text=True, capture_output=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        raise RuntimeError(f"mutool draw failed: {result.stderr.strip()}")
    width, height, dark = read_ppm_dark(output)
    return {"width": width, "height": height, "dark": dark}


def mupdf_render_pymupdf(pdf: Path, dpi: float, workdir: Path, page: int) -> dict[str, object]:
    import fitz
    document = fitz.open(str(pdf))
    page_object = document[page - 1]
    matrix = fitz.Matrix(dpi / 72.0, dpi / 72.0)
    pixmap = page_object.get_pixmap(matrix=matrix, alpha=False)
    output = workdir / "mupdf-page-1.ppm"
    pixmap.save(str(output))
    width, height, dark = read_ppm_dark(output)
    return {"width": width, "height": height, "dark": dark}


def mupdf_render(pdf: Path, dpi: float, workdir: Path, page: int) -> dict[str, object]:
    if shutil.which("mutool"):
        return mupdf_render_mutool(pdf, dpi, workdir, page)
    try:
        return mupdf_render_pymupdf(pdf, dpi, workdir, page)
    except ImportError:
        raise RuntimeError(
            "No MuPDF backend found: install mupdf-tools (mutool) or PyMuPDF.")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--pdfpp", type=Path, required=True)
    parser.add_argument("--dpi", type=float, default=72.0)
    parser.add_argument("--page", type=int, default=1)
    parser.add_argument("--coverage-tolerance", type=float, default=0.20)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="pdfpp-diff-") as directory:
        workdir = Path(directory)
        pdfpp = parse_render_line(args.pdfpp, args.input, args.dpi, workdir, args.page)
        mupdf = mupdf_render(args.input, args.dpi, workdir, args.page)

    pdfpp_pixels = pdfpp["width"] * pdfpp["height"]
    mupdf_pixels = mupdf["width"] * mupdf["height"]
    pdfpp_ratio = pdfpp["dark"] / pdfpp_pixels if pdfpp_pixels else 0.0
    mupdf_ratio = mupdf["dark"] / mupdf_pixels if mupdf_pixels else 0.0

    result = {
        "input": str(args.input),
        "page": args.page,
        "pdfpp": pdfpp,
        "mupdf": mupdf,
        "pdfpp_dark_ratio": round(pdfpp_ratio, 4),
        "mupdf_dark_ratio": round(mupdf_ratio, 4),
        "dimension_match": pdfpp["width"] == mupdf["width"]
            and pdfpp["height"] == mupdf["height"],
        "coverage_delta": round(abs(pdfpp_ratio - mupdf_ratio), 4),
    }

    if not result["dimension_match"]:
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 1

    if result["coverage_delta"] > args.coverage_tolerance:
        result["warning"] = (
            "dark-pixel coverage differs from MuPDF beyond tolerance; "
            "expected for ASCII fallback text or differing font rasterization")
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 2

    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

