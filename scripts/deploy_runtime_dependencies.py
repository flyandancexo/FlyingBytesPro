#!/usr/bin/env python3
# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

"""Copy non-system PE dependencies from an MSYS2 UCRT64 bin directory.

This complements windeployqt. It recursively scans every EXE and DLL in the
portable folder with objdump, copies dependencies found in UCRT64/bin, and
fails if a non-system dependency remains unresolved.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from collections import deque
from pathlib import Path

DLL_RE = re.compile(r"^\s*DLL Name:\s*(.+?)\s*$", re.IGNORECASE)
SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-win-")
PE_SUFFIXES = {".exe", ".dll"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dist", required=True, type=Path)
    parser.add_argument("--ucrt-bin", required=True, type=Path)
    parser.add_argument("--objdump", required=True, type=Path)
    parser.add_argument("--windows-dir", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    return parser.parse_args()


def build_file_index(directories: list[Path]) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for directory in directories:
        if not directory.is_dir():
            continue
        try:
            for child in directory.iterdir():
                if child.is_file():
                    result.setdefault(child.name.casefold(), child)
        except OSError:
            continue
    return result


def build_dist_index(dist: Path) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for path in dist.rglob("*"):
        if path.is_file():
            result.setdefault(path.name.casefold(), path)
    return result


def imported_dlls(objdump: Path, pe_file: Path) -> list[str]:
    completed = subprocess.run(
        [str(objdump), "-p", str(pe_file)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"objdump failed for {pe_file}: {completed.stderr.strip()}"
        )
    names: list[str] = []
    for line in completed.stdout.splitlines():
        match = DLL_RE.match(line)
        if match:
            names.append(match.group(1).strip())
    return names


def is_system_dependency(name: str, system_index: dict[str, Path]) -> bool:
    folded = name.casefold()
    if folded.startswith(SYSTEM_PREFIXES):
        return True
    return folded in system_index


def main() -> int:
    args = parse_args()
    dist = args.dist.resolve()
    ucrt_bin = args.ucrt_bin.resolve()
    objdump = args.objdump.resolve()
    windows_dir = args.windows_dir.resolve()
    report = args.report.resolve()

    if not dist.is_dir():
        print(f"ERROR: portable folder not found: {dist}", file=sys.stderr)
        return 2
    if not ucrt_bin.is_dir():
        print(f"ERROR: UCRT64 bin folder not found: {ucrt_bin}", file=sys.stderr)
        return 2
    if not objdump.is_file():
        print(f"ERROR: objdump not found: {objdump}", file=sys.stderr)
        return 2

    windows_dirs = [
        windows_dir,
        windows_dir / "System32",
        windows_dir / "SysWOW64",
    ]
    system_index = build_file_index(windows_dirs)
    ucrt_index = build_file_index([ucrt_bin])

    queue: deque[Path] = deque(
        path
        for path in dist.rglob("*")
        if path.is_file() and path.suffix.casefold() in PE_SUFFIXES
    )
    scanned: set[Path] = set()
    copied: list[tuple[str, Path, Path]] = []
    unresolved: list[tuple[Path, str]] = []

    while queue:
        current = queue.popleft().resolve()
        if current in scanned:
            continue
        scanned.add(current)

        try:
            imports = imported_dlls(objdump, current)
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 3

        dist_index = build_dist_index(dist)
        for dependency in imports:
            key = dependency.casefold()
            if key in dist_index:
                continue
            if is_system_dependency(dependency, system_index):
                continue

            source = ucrt_index.get(key)
            if source is None:
                unresolved.append((current, dependency))
                continue

            destination = dist / source.name
            shutil.copy2(source, destination)
            copied.append((dependency, source, destination))
            queue.append(destination)

    # Re-check unresolved entries after recursive copying; another scanned DLL
    # may have copied the same dependency later in the pass.
    final_index = build_dist_index(dist)
    final_unresolved: list[tuple[Path, str]] = []
    for source_file, dependency in unresolved:
        if dependency.casefold() in final_index:
            continue
        if is_system_dependency(dependency, system_index):
            continue
        final_unresolved.append((source_file, dependency))

    lines: list[str] = []
    lines.append("FlyingBytesPro portable dependency report")
    lines.append(f"Portable folder: {dist}")
    lines.append(f"PE files scanned: {len(scanned)}")
    lines.append(f"Runtime DLLs copied from UCRT64: {len(copied)}")
    lines.append("")

    if copied:
        lines.append("Copied files:")
        for dependency, source, destination in copied:
            lines.append(f"  {dependency}: {source} -> {destination}")
        lines.append("")

    if final_unresolved:
        lines.append("UNRESOLVED NON-SYSTEM DEPENDENCIES:")
        for source_file, dependency in final_unresolved:
            lines.append(f"  {source_file.relative_to(dist)} requires {dependency}")
    else:
        lines.append("PASS: no unresolved non-system PE dependencies were found.")

    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))

    return 4 if final_unresolved else 0


if __name__ == "__main__":
    raise SystemExit(main())
