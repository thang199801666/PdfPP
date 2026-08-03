"""Differential text extraction: compare Pdf++ output with MuPDF (PyMuPDF).

Runs PdfPP.Inspect to obtain per-page extracted text and MuPDF (PyMuPDF) for the
same pages, then reports token-level overlap per page. Page-count mismatches and
pages with near-zero overlap are hard failures; lower-but-nonzero overlap is
warned so spacing/ordering differences between engines do not fail the run.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections.abc import Iterable
from pathlib import Path


def tokenize(text: str) -> set[str]:
    return set(re.findall(r"[A-Za-z0-9_]+", text))


def overlap(a: str, b: str) -> float:
    tokens_a = tokenize(a)
    tokens_b = tokenize(b)
    if not tokens_a and not tokens_b:
        return 1.0
    if not tokens_a or not tokens_b:
        return 0.0
    return len(tokens_a & tokens_b) / len(tokens_a)


def pdfpp_text(executable: Path, pdf: Path) -> dict[int, str]:
    result = subprocess.run([str(executable), str(pdf)], check=True, text=True,
                            capture_output=True, encoding="utf-8", errors="replace")
    pages: dict[int, str] = {}
    for line in result.stdout.splitlines():
        fields = line.split("\t", 2)
        if fields[0] == "page_text":
            pages[int(fields[1])] = fields[2] if len(fields) == 3 else ""
    return pages


def mupdf_text(pdf: Path) -> dict[int, str]:
    import fitz
    document = fitz.open(str(pdf))
    return {index: page.get_text("text") or "" for index, page in enumerate(document)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--pdfpp", type=Path, required=True)
    parser.add_argument("--overlap-threshold", type=float, default=0.10,
                        help="minimum per-page token overlap (default 0.10)")
    args = parser.parse_args()

    pdfpp = pdfpp_text(args.pdfpp, args.input)
    mupdf = mupdf_text(args.input)

    pages = max(len(pdfpp), len(mupdf))
    low_pages: list[dict[str, object]] = []
    for page in range(pages):
        pdfpp_page = pdfpp.get(page, "")
        mupdf_page = mupdf.get(page, "")
        if not pdfpp_page and not mupdf_page:
            continue
        score = overlap(pdfpp_page, mupdf_page)
        if score < args.overlap_threshold:
            low_pages.append({
                "page": page,
                "overlap": round(score, 3),
                "pdfpp_length": len(pdfpp_page),
                "mupdf_length": len(mupdf_page),
            })

    result = {
        "input": str(args.input),
        "pdfpp_pages": len(pdfpp),
        "mupdf_pages": len(mupdf),
        "pages_compared": pages,
        "low_overlap_pages": low_pages,
        "low_overlap_count": len(low_pages),
    }

    if result["pdfpp_pages"] != result["mupdf_pages"]:
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 1
    if result["low_overlap_count"] > 0:
        result["warning"] = (
            "one or more pages have low token overlap with MuPDF; expected for "
            "complex layouts, images, or differing text-reconstruction rules")
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 2

    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
