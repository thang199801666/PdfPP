"""Cross-engine feature and end-to-end benchmark matrix for Pdf++, MuPDF and pypdf."""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import fitz
from PIL import Image
from pypdf import PdfReader
from reportlab.lib.pagesizes import letter
from reportlab.lib.utils import ImageReader
from reportlab.pdfgen import canvas


def generate_corpus(path: Path) -> None:
    image_path = path.with_name("matrix-image.png")
    Image.new("RGB", (160, 80), (30, 120, 210)).save(image_path)
    pdf = canvas.Canvas(str(path), pagesize=letter, pageCompression=1)
    pdf.setTitle("PdfPP feature matrix")
    pdf.setAuthor("PdfPP validation")
    for index in range(5):
        pdf.bookmarkPage(f"page-{index}")
        pdf.addOutlineEntry(f"Page {index + 1}", f"page-{index}", level=0, closed=False)
        pdf.setFont("Helvetica", 16)
        pdf.drawString(72, 720, f"Feature matrix page {index + 1}")
        pdf.setFont("Helvetica", 11)
        pdf.drawString(72, 690, f"TEXT-MARKER-{index + 1:02d} Unicode-compatible ASCII text")
        pdf.rect(72, 560, 180, 80, stroke=1, fill=0)
        pdf.drawImage(ImageReader(str(image_path)), 300, 560, width=160, height=80)
        pdf.linkURL("https://example.com/pdfpp", (72, 520, 220, 540), relative=0)
        if index == 0:
            pdf.acroForm.textfield(name="validation_name", x=72, y=450,
                                   width=220, height=22, value="PdfPP")
        pdf.showPage()
    pdf.save()


def median_ms(operation, iterations: int) -> float:
    samples = []
    for _ in range(iterations):
        start = time.perf_counter()
        operation()
        samples.append((time.perf_counter() - start) * 1000.0)
    return round(statistics.median(samples), 3)


def pdfinfo_pages(executable: Path, path: Path) -> int:
    output = subprocess.run([str(executable), str(path)], check=True,
                            text=True, capture_output=True).stdout
    return next(int(line.split(":", 1)[1].strip()) for line in output.splitlines()
                if line.startswith("Pages:"))


def inspect_pdfpp(executable: Path, path: Path) -> dict[str, object]:
    result = subprocess.run([str(executable), str(path)], check=True,
                            text=True, capture_output=True)
    values: dict[str, object] = {"page_text": {}, "page_images": {}}
    for line in result.stdout.splitlines():
        fields = line.split("\t", 2)
        if fields[0] == "page_text":
            values["page_text"][int(fields[1])] = fields[2]
        elif fields[0] == "page_images":
            values["page_images"][int(fields[1])] = int(fields[2])
        elif len(fields) == 2:
            values[fields[0]] = int(fields[1]) if fields[1].isdigit() else fields[1]
    return values


def inspect_pypdf(path: Path) -> dict[str, object]:
    reader = PdfReader(str(path))
    texts = [page.extract_text() or "" for page in reader.pages]
    images = []
    links = []
    widgets = []
    for page in reader.pages:
        try:
            images.append(len(page.images))
        except Exception:
            images.append(0)
        annotations = page.get("/Annots", [])
        links.append(sum(1 for annotation in annotations
                         if annotation.get_object().get("/Subtype") == "/Link"))
        widgets.append(sum(1 for annotation in annotations
                           if annotation.get_object().get("/Subtype") == "/Widget"))
    return {
        "pages": len(reader.pages),
        "title": reader.metadata.title if reader.metadata else None,
        "author": reader.metadata.author if reader.metadata else None,
        "page_text": texts,
        "page_images": images,
        "links": sum(links),
        "widgets": sum(widgets),
        "fields": sorted((reader.get_fields() or {}).keys()),
        "outlines": len(reader.outline) if hasattr(reader, "outline") else 0,
    }


def inspect_mupdf(path: Path) -> dict[str, object]:
    document = fitz.open(str(path))
    return {
        "pages": len(document),
        "title": document.metadata.get("title"),
        "author": document.metadata.get("author"),
        "page_text": [page.get_text("text") for page in document],
        "page_images": [len(page.get_images(full=True)) for page in document],
        "links": sum(len(page.get_links()) for page in document),
        "widgets": sum(len(list(page.widgets() or [])) for page in document),
        "outlines": len(document.get_toc()),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pdfpp", type=Path, required=True)
    parser.add_argument("--pdfinfo", type=Path, required=True)
    parser.add_argument("--pdftoppm", type=Path)
    parser.add_argument("--workdir", type=Path, default=Path("tmp/pdfs/matrix"))
    parser.add_argument("--iterations", type=int, default=5)
    args = parser.parse_args()
    args.workdir.mkdir(parents=True, exist_ok=True)
    corpus = args.workdir / "feature-matrix.pdf"
    generate_corpus(corpus)

    pdfpp = inspect_pdfpp(args.pdfpp, corpus)
    pypdf = inspect_pypdf(corpus)
    mupdf = inspect_mupdf(corpus)
    poppler_pages = pdfinfo_pages(args.pdfinfo, corpus)

    marker_ok = all(f"TEXT-MARKER-{i + 1:02d}" in text
                    for i, text in enumerate(pypdf["page_text"]))
    pdfpp_marker_ok = all(f"TEXT-MARKER-{i + 1:02d}" in pdfpp["page_text"].get(i, "")
                          for i in range(5))
    mupdf_marker_ok = all(f"TEXT-MARKER-{i + 1:02d}" in text
                          for i, text in enumerate(mupdf["page_text"]))

    with tempfile.TemporaryDirectory(dir=args.workdir) as temp:
        render_dir = Path(temp)
        timings = {
            "pdfpp_open_text_images_process_ms": median_ms(
                lambda: inspect_pdfpp(args.pdfpp, corpus), args.iterations),
            "pypdf_open_text_ms": median_ms(
                lambda: [page.extract_text() for page in PdfReader(str(corpus)).pages], args.iterations),
            "mupdf_open_text_ms": median_ms(
                lambda: [page.get_text("text") for page in fitz.open(str(corpus))], args.iterations),
            "mupdf_render_first_page_ms": median_ms(
                lambda: fitz.open(str(corpus))[0].get_pixmap(alpha=False), args.iterations),
            "poppler_page_count_ms": median_ms(
                lambda: pdfinfo_pages(args.pdfinfo, corpus), args.iterations),
        }
        render_dir.mkdir(parents=True, exist_ok=True)
        render_path = render_dir / "poppler-page-1"
        pdftoppm = args.pdftoppm or shutil.which("pdftoppm")
        if pdftoppm:
            timings["poppler_render_first_page_ms"] = median_ms(
                lambda: subprocess.run([pdftoppm, "-f", "1", "-l", "1", "-singlefile",
                                        "-png", str(corpus), str(render_path)],
                                       check=True, capture_output=True), args.iterations)

    result = {
        "corpus": str(corpus),
        "mupdf_version": fitz.VersionBind,
        "features": {
            "page_count": {"PdfPP": pdfpp.get("pages"), "MuPDF": mupdf["pages"],
                           "pypdf": pypdf["pages"], "Poppler": poppler_pages},
            "metadata": {"PdfPP": [pdfpp.get("title"), pdfpp.get("author")], "MuPDF": [mupdf["title"], mupdf["author"]],
                         "pypdf": [pypdf["title"], pypdf["author"]]},
            "text_markers": {"PdfPP": pdfpp_marker_ok, "MuPDF": mupdf_marker_ok,
                             "pypdf": marker_ok},
            "images": {"PdfPP": list(pdfpp["page_images"].values()),
                       "MuPDF": mupdf["page_images"], "pypdf": pypdf["page_images"]},
            "links": {"MuPDF": mupdf["links"], "pypdf": pypdf["links"]},
            "acroform_widgets": {"MuPDF": mupdf["widgets"], "pypdf": pypdf["widgets"]},
            "outlines": {"MuPDF": mupdf["outlines"], "pypdf": pypdf["outlines"]},
        },
        "timings_median_ms": timings,
        "notes": [
            "Pdf++ semantic results come from PdfPP.Inspect; process startup is included.",
            "pypdf is a parser, not a renderer; rendering comparisons use MuPDF and Poppler.",
            "Run with the same machine, build type, corpus and iteration count before comparing timings.",
        ],
    }
    print(json.dumps(result, indent=2, default=str))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
