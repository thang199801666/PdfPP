"""Compare a PDF with MuPDF through the PyMuPDF binding."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import fitz


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--render", type=Path)
    args = parser.parse_args()

    document = fitz.open(str(args.input))
    texts = [page.get_text("text") for page in document]
    result = {
        "engine": "MuPDF",
        "version": fitz.VersionBind,
        "pages": len(document),
        "title": document.metadata.get("title"),
        "author": document.metadata.get("author"),
        "text_lengths": [len(text) for text in texts],
    }
    if args.render:
        args.render.parent.mkdir(parents=True, exist_ok=True)
        document[0].get_pixmap(matrix=fitz.Matrix(1, 1), alpha=False).save(str(args.render))
        result["rendered_page"] = str(args.render)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
