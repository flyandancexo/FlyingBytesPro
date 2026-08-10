#!/usr/bin/env python3
# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

"""Reject filenames that collide on case-insensitive Windows filesystems."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
seen: dict[str, str] = {}
errors: list[tuple[str, str]] = []

for path in sorted(root.rglob('*')):
    if not path.is_file():
        continue
    rel = path.relative_to(root).as_posix()
    key = rel.casefold()
    previous = seen.get(key)
    if previous is not None and previous != rel:
        errors.append((previous, rel))
    else:
        seen[key] = rel

if errors:
    print('ERROR: filenames collide on Windows:')
    for first, second in errors:
        print(f'  {first}')
        print(f'  {second}')
    sys.exit(1)

print(f'Windows filename validation passed: {len(seen)} files checked.')
