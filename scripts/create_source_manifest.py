#!/usr/bin/env python3
# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

"""Write deterministic SHA-256 hashes for source-package files."""

from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "SOURCE_MANIFEST_SHA256.txt"
EXCLUDED_PARTS = {"build-ucrt64-release", "dist", "__pycache__"}
EXCLUDED_NAMES = {"BUILD_LOG.txt", "BUILD_RESULT.txt", OUTPUT.name}


def main() -> int:
  entries: list[tuple[str, str]] = []
  for path in sorted(ROOT.rglob("*"), key=lambda p: p.as_posix().casefold()):
    if not path.is_file():
      continue
    relative = path.relative_to(ROOT)
    if any(part in EXCLUDED_PARTS for part in relative.parts):
      continue
    if path.name in EXCLUDED_NAMES or path.suffix == ".pyc":
      continue
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    entries.append((digest, relative.as_posix()))
  text = "\n".join(f"{digest}  {name}" for digest, name in entries) + "\n"
  OUTPUT.write_text(text, encoding="utf-8", newline="\n")
  print(f"Wrote {len(entries)} hashes to {OUTPUT.name}.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
