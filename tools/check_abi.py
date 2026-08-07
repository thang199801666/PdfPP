#!/usr/bin/env python3
"""Check that the stable Pdf++ C ABI entry points are exported."""
from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys

REQUIRED = {
    "pdfpp_c_version",
    "pdfpp_version",
    "pdfpp_open",
    "pdfpp_close",
    "pdfpp_page_count",
}


def exported_symbols(library: pathlib.Path) -> str:
    if sys.platform == "win32":
        tool = shutil.which("dumpbin")
        if not tool:
            raise RuntimeError("dumpbin was not found")
        # Static .lib archives have no export table. /symbols works for both
        # regular static libraries and import libraries; DLLs use /exports.
        command = [tool, "/exports" if library.suffix.lower() == ".dll" else "/symbols", str(library)]
    else:
        tool = shutil.which("nm")
        if not tool:
            raise RuntimeError("nm was not found")
        # -D only inspects a shared object's dynamic symbol table. Static
        # archives require the regular global-symbol table.
        if library.suffix.lower() == ".a":
            command = [tool, "-g", "--defined-only", str(library)]
        else:
            command = [tool, "-D", "--defined-only", str(library)]
    return subprocess.run(command, check=True, text=True, capture_output=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=pathlib.Path)
    args = parser.parse_args()
    symbols = exported_symbols(args.library)
    missing = sorted(name for name in REQUIRED if name not in symbols)
    if missing:
        print("Missing C ABI symbols: " + ", ".join(missing), file=sys.stderr)
        return 1
    print("C ABI check passed: " + ", ".join(sorted(REQUIRED)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
