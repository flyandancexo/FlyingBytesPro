#!/usr/bin/env python3
# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

FUSE_KEYS = {
  "LOW": ("low", 0),
  "HIGH": ("high", 1),
  "EXTENDED": ("extended", 2),
  "BYTE0": ("low", 0),
}


def parse_int(text: str | None, default: int = 0) -> int:
  if text is None:
    return default
  value = text.strip()
  try:
    return int(value, 0)
  except ValueError:
    try:
      return int(value, 16)
    except ValueError:
      return default


def signature_text(root: ET.Element) -> str:
  values = []
  for tag in ("ADDR000", "ADDR001", "ADDR002"):
    values.append(parse_int(root.findtext(f"./ADMIN/SIGNATURE/{tag}"), 0))
  return "".join(f"{value & 0xFF:02X}" for value in values)


def parse_bits(section: ET.Element, prefix: str) -> list[dict]:
  bits = []
  for child in section:
    match = re.fullmatch(rf"{prefix}(\d+)", child.tag, re.IGNORECASE)
    if not match:
      continue
    bit = int(match.group(1))
    name = (child.findtext("NAME") or f"Bit {bit}").strip()
    description = (child.findtext("TEXT") or "").strip()
    default_value = parse_int(child.findtext("DEFAULT"), -1)
    bits.append({
      "bit": bit,
      "name": name,
      "description": description,
      "default": default_value,
    })
  return sorted(bits, key=lambda item: item["bit"], reverse=True)




def normalize_option_text(text: str) -> str:
  text = re.sub(r"Ext\. RC Osc\.\s*-\s*0\.9 MHz",
                "Ext. RC Osc. 0.1 MHz - 0.9 MHz", text)
  return re.sub(r"[ \t]+", " ", text).strip()

def parse_options(section: ET.Element) -> list[dict]:
  options = []
  for child in section:
    if not re.fullmatch(r"TEXT\d+", child.tag, re.IGNORECASE):
      continue
    text = normalize_option_text(child.findtext("TEXT") or "")
    if not text:
      continue
    options.append({
      "mask": parse_int(child.findtext("MASK"), 0) & 0xFF,
      "value": parse_int(child.findtext("VALUE"), 0) & 0xFF,
      "text": text,
    })
  return options


def parse_mask_list(text: str | None) -> list[int]:
  if not text:
    return []
  return [parse_int(part.strip(), 0xFF) & 0xFF for part in text.split(",")]


def parse_warnings(root: ET.Element) -> list[dict]:
  warnings = []
  interface = root.find("./PROGRAMMING/ISPInterface")
  if interface is None:
    return warnings
  for node in interface.findall("FuseWarning"):
    text = (node.text or "").strip()
    parts = [part.strip() for part in text.split(",", 3)]
    if len(parts) != 4:
      continue
    warnings.append({
      "byteIndex": parse_int(parts[0], 0),
      "mask": parse_int(parts[1], 0) & 0xFF,
      "value": parse_int(parts[2], 0) & 0xFF,
      "text": parts[3],
    })
  return warnings


def parse_file(path: Path) -> dict | None:
  try:
    raw = path.read_bytes()
    marker = raw.find(b"<CHINESE>")
    if marker >= 0:
      raw = raw[:marker] + b"</AVRPART>"
    root = ET.fromstring(raw.decode("utf-8", errors="ignore"))
  except (ET.ParseError, OSError, UnicodeError):
    return None
  if root.tag.upper() != "AVRPART":
    return None

  name = (root.findtext("./ADMIN/PART_NAME") or path.stem).strip()
  fuses = []
  read_masks = parse_mask_list(root.findtext("./PROGRAMMING/ISPInterface/FuseReadMask"))
  program_masks = parse_mask_list(root.findtext("./PROGRAMMING/ISPInterface/FuseProgMask"))
  fuse_root = root.find("FUSE")
  if fuse_root is not None:
    for child in fuse_root:
      mapping = FUSE_KEYS.get(child.tag.upper())
      if mapping is None:
        continue
      key, byte_index = mapping
      fuses.append({
        "key": key,
        "byteIndex": byte_index,
        "readMask": read_masks[byte_index] if byte_index < len(read_masks) else 0xFF,
        "programMask": program_masks[byte_index] if byte_index < len(program_masks) else 0xFF,
        "bits": parse_bits(child, "FUSE"),
        "options": parse_options(child),
      })

  lock = None
  lock_root = root.find("LOCKBIT")
  if lock_root is not None:
    lock = {
      "bits": parse_bits(lock_root, "LOCKBIT"),
      "options": parse_options(lock_root),
      "description": (lock_root.findtext("TEXT") or "").strip(),
    }

  if not fuses and lock is None:
    return None
  return {
    "name": name,
    "signature": signature_text(root),
    "fuses": sorted(fuses, key=lambda item: item["byteIndex"]),
    "lock": lock,
    "warnings": parse_warnings(root),
  }


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("xml_dir", type=Path)
  parser.add_argument("output", type=Path)
  args = parser.parse_args()

  devices = []
  for path in sorted(args.xml_dir.glob("*.xml")) + sorted(args.xml_dir.glob("*.XML")):
    device = parse_file(path)
    if device is not None:
      devices.append(device)

  result = {
    "schemaVersion": 1,
    "source": "ProgISP 1.72 English device XML metadata",
    "devices": devices,
  }
  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
  print(f"Wrote {len(devices)} fuse/lock metadata entries to {args.output}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
