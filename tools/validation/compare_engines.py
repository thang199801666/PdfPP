"""Generate and compare a small PDF corpus using Pdf++, pypdf and Poppler."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from pypdf import PdfReader
from reportlab.lib.pagesizes import letter
from reportlab.pdfgen import canvas


def generate_corpus(path: Path) -> None:
    pdf = canvas.Canvas(str(path), pagesize=letter, pageCompression=1)
    for index in range(3):
        pdf.setTitle("PdfPP validation corpus")
        pdf.setAuthor("PdfPP validation")
        pdf.setFont("Helvetica", 16)
        pdf.drawString(72, 720, f"PdfPP validation page {index + 1}")
        pdf.setFont("Helvetica", 11)
        pdf.drawString(72, 690, "ASCII text: finite element PDF compatibility")
        pdf.drawString(72, 670, f"Page marker: VALIDATION-{index + 1:02d}")
        pdf.rect(72, 560, 180, 80, stroke=1, fill=0)
        pdf.showPage()
    pdf.save()


def poppler_pages(path: Path) -> int:
    pdfinfo = shutil.which("pdfinfo")
    if not pdfinfo:
        raise RuntimeError("pdfinfo was not found")
    result = subprocess.run([pdfinfo, str(path)], check=True, text=True,
                            capture_output=True)
    for line in result.stdout.splitlines():
        if line.startswith("Pages:"):
            return int(line.split(":", 1)[1].strip())
    raise RuntimeError("Poppler did not report a page count")


def pdfpp_inspect(executable: Path, path: Path) -> dict[str, object]:
    result = subprocess.run([str(executable), str(path)], check=True,
                            text=True, capture_output=True)
    values: dict[str, object] = {"page_text": {}}
    for line in result.stdout.splitlines():
        fields = line.split("\t", 2)
        if fields[0] == "page_text":
            values["page_text"][int(fields[1])] = fields[2]
        elif len(fields) == 2:
            values[fields[0]] = int(fields[1]) if fields[1].isdigit() else fields[1]
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pdfpp", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, default=Path("tmp/pdfs/validation"))
    parser.add_argument("--pdfinfo", type=Path)
    parser.add_argument("--pdftoppm", type=Path)
    args = parser.parse_args()

    args.workdir.mkdir(parents=True, exist_ok=True)
    corpus = args.workdir / "reportlab_text_shapes.pdf"
    render_prefix = args.workdir / "rendered"
    generate_corpus(corpus)

    reader = PdfReader(str(corpus))
    pypdf_text = [page.extract_text() or "" for page in reader.pages]
    pdfinfo_path = args.pdfinfo or shutil.which("pdfinfo")
    if not pdfinfo_path:
        raise RuntimeError("pdfinfo was not found; pass --pdfinfo explicitly")
    result = subprocess.run([str(pdfinfo_path), str(corpus)], check=True, text=True,
                            capture_output=True)
    poppler_count = next(
        int(line.split(":", 1)[1].strip())
        for line in result.stdout.splitlines() if line.startswith("Pages:")
    )
    pdfpp = pdfpp_inspect(args.pdfpp, corpus)

    result = {
        "input": str(corpus),
        "pypdf_pages": len(reader.pages),
        "poppler_pages": poppler_count,
        "pdfpp_pages": pdfpp.get("pages"),
        "pypdf_contains_marker": all(f"VALIDATION-{i + 1:02d}" in text
                                       for i, text in enumerate(pypdf_text)),
        "pdfpp_contains_marker": all(f"VALIDATION-{i + 1:02d}" in pdfpp["page_text"].get(i, "")
                                      for i in range(len(reader.pages))),
    }
    if result["pypdf_pages"] != result["poppler_pages"] or result["pypdf_pages"] != result["pdfpp_pages"]:
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 1
    if not result["pypdf_contains_marker"] or not result["pdfpp_contains_marker"]:
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 1

    pdftoppm_path = args.pdftoppm or shutil.which("pdftoppm")
    if pdftoppm_path:
        subprocess.run([str(pdftoppm_path), "-f", "1", "-l", "1", "-png", str(corpus), str(render_prefix)],
                       check=True)
        result["rendered_page"] = str(render_prefix) + "-1.png"
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
