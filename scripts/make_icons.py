#!/usr/bin/env python3
# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import cairosvg
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = root / 'resources' / 'icons'
out.mkdir(parents=True, exist_ok=True)
src = root / 'resources' / 'icon_sources' / 'tabler'

icons = {
    'detect_target': ('target-arrow.svg', '#2463a9'),
    'load': ('folder-open.svg', '#2463a9'),
    'save': ('device-floppy.svg', '#2463a9'),
    'write': ('upload.svg', '#bd1e2d'),
    'read': ('download.svg', '#00a86b'),
    'start': ('player-play.svg', '#00a86b'),
    'project_load': ('package-import.svg', '#6b45a8'),
    'full_log': ('terminal-2.svg', '#364152'),
    'about': ('info-circle.svg', '#364152'),
}

for name, (filename, color) in icons.items():
    text = (src / filename).read_text(encoding='utf-8')
    text = text.replace('currentColor', color)
    png = out / f'{name}.png'
    cairosvg.svg2png(bytestring=text.encode('utf-8'), write_to=str(png), output_width=96, output_height=96)

# Build the application icon from the supplied Flyandance size-specific artwork.
flyandance = root / 'resources' / 'icon_sources' / 'flyandance'
source_by_size = {
    256: flyandance / 'FD-A1.png',
    128: flyandance / 'FD-A2.png',
    64: flyandance / 'FD-A3.png',
    32: flyandance / 'FD-A4.png',
}

for size, source_path in source_by_size.items():
    image = Image.open(source_path).convert('RGBA')
    image.resize((size, size), Image.Resampling.LANCZOS).save(
        out / f'app_{size}.png', optimize=True
    )

for size, source_size in ((48, 64), (24, 32), (16, 32)):
    image = Image.open(out / f'app_{source_size}.png').convert('RGBA')
    image.resize((size, size), Image.Resampling.LANCZOS).save(
        out / f'app_{size}.png', optimize=True
    )

# Write a multi-resolution ICO containing the actual PNG prepared for each size.
# PNG-compressed ICO entries preserve transparency and avoid re-rendering the
# smaller supplied artwork from only the 256-pixel source.
import struct

sizes = (16, 24, 32, 48, 64, 128, 256)
png_data = [(out / f'app_{size}.png').read_bytes() for size in sizes]
header_size = 6 + 16 * len(sizes)
offset = header_size
entries = []
for size, data in zip(sizes, png_data):
    width = 0 if size == 256 else size
    height = 0 if size == 256 else size
    entries.append(struct.pack(
        '<BBBBHHII', width, height, 0, 0, 1, 32, len(data), offset
    ))
    offset += len(data)

ico = struct.pack('<HHH', 0, 1, len(sizes)) + b''.join(entries) + b''.join(png_data)
(root / 'resources' / 'windows' / 'app.ico').write_bytes(ico)
