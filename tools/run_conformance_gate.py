#!/usr/bin/env python3
"""Run veraPDF gates for Pdf++ PDF/A + PDF/UA conformance fixtures."""
from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys


def run(verapdf: str, flavour: str, pdf: pathlib.Path) -> bool:
    command = [verapdf, "--format", "text", "--flavour", flavour, str(pdf)]
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(command, text=True)
    return result.returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verapdf", default="verapdf")
    parser.add_argument("--pdfa4f-ua2", type=pathlib.Path, required=True)
    parser.add_argument("--pdfa2a-ua1", type=pathlib.Path, required=True)
    args = parser.parse_args()

    executable = shutil.which(args.verapdf) if not pathlib.Path(args.verapdf).is_file() else args.verapdf
    if not executable:
        print("veraPDF executable was not found", file=sys.stderr)
        return 2
    for path in (args.pdfa4f_ua2, args.pdfa2a_ua1):
        if not path.is_file():
            print(f"Missing conformance fixture: {path}", file=sys.stderr)
            return 2

    checks = (
        ("4f", args.pdfa4f_ua2),
        ("ua2", args.pdfa4f_ua2),
        ("2a", args.pdfa2a_ua1),
        ("ua1", args.pdfa2a_ua1),
    )
    passed = all(run(str(executable), flavour, path) for flavour, path in checks)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
