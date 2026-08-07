#!/usr/bin/env python3
"""Validate the Pdf++ source and Visual Studio project layout."""
from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

MSBUILD_NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
ITEM_TAGS = ("ClCompile", "ClInclude", "ProjectReference", "None", "ResourceCompile")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    errors: list[str] = []

    canonical = root / "src" / "Pdf++.Core"
    if not canonical.is_dir():
        errors.append("Canonical Core directory is missing: src/Pdf++.Core")
    if (root / "Pdf++.Core.vcxproj").exists():
        errors.append("Duplicate root Pdf++.Core.vcxproj must not exist")
    if (root / "include" / "CPPPdf").exists():
        errors.append("Duplicate root include/CPPPdf tree must not exist")

    solution = root / "Pdf++.sln"
    if not solution.is_file():
        errors.append("Pdf++.sln is missing")
    else:
        text = solution.read_text(encoding="utf-8", errors="replace")
        for name, relative in re.findall(
            r'Project\("[^"]+"\) = "([^"]+)", "([^"]+)",', text
        ):
            path = root / relative.replace("\\", "/")
            if not path.exists():
                errors.append(f"Solution project is missing: {name}: {relative}")

    for project in root.rglob("*.vcxproj"):
        try:
            tree = ET.parse(project)
        except ET.ParseError as error:
            errors.append(f"Invalid project XML: {project.relative_to(root)}: {error}")
            continue
        for tag in ITEM_TAGS:
            for item in tree.getroot().findall(f".//m:{tag}", MSBUILD_NS):
                include = item.attrib.get("Include")
                if not include or "$(" in include or "%(" in include:
                    continue
                path = (project.parent / include.replace("\\", "/")).resolve()
                if not path.exists():
                    errors.append(
                        f"Missing {tag} item in {project.relative_to(root)}: {include}"
                    )

    if errors:
        print("Project layout validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("Project layout validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
