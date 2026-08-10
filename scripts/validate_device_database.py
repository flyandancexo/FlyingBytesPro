#!/usr/bin/env python3
# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

"""Validate the embedded AVR device database without requiring Qt."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from collections import defaultdict

HEX8 = re.compile(r"^[0-9A-Fa-f]{8}$")
HEX6 = re.compile(r"^[0-9A-Fa-f]{6}$")


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    default_path = pathlib.Path(__file__).resolve().parents[1] / "resources/devices/avr_devices.json"
    parser = argparse.ArgumentParser()
    parser.add_argument("database", nargs="?", type=pathlib.Path, default=default_path)
    args = parser.parse_args()

    try:
        document = json.loads(args.database.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read {args.database}: {exc}")

    if document.get("schemaVersion") != 1:
        fail("schemaVersion must be 1")

    devices = document.get("devices")
    if not isinstance(devices, list) or not devices:
        fail("top-level 'devices' must be a nonempty list")

    ids: set[str] = set()
    names: set[str] = set()
    signatures: dict[str, list[str]] = defaultdict(list)
    flash_signature_matches = 0
    page_signature_matches = 0
    page_signature_exceptions: list[tuple[str, int, int]] = []
    tpi_devices = 0

    for index, device in enumerate(devices):
        where = f"device[{index}]"
        if not isinstance(device, dict):
            fail(f"{where} is not an object")

        device_id = device.get("id")
        name = device.get("name")
        signature = device.get("signature")
        if not isinstance(device_id, str) or not device_id:
            fail(f"{where}.id is missing")
        if device_id in ids:
            fail(f"duplicate id: {device_id}")
        ids.add(device_id)

        if not isinstance(name, str) or not name:
            fail(f"{where}.name is missing")
        if name.casefold() in names:
            fail(f"duplicate name: {name}")
        names.add(name.casefold())

        if not isinstance(signature, str) or not HEX6.fullmatch(signature):
            fail(f"{name}: signature must be exactly three hexadecimal bytes")
        signatures[signature.upper()].append(name)

        for field in ("flashSize", "flashPageSize", "eepromSize", "eepromPageSize"):
            value = device.get(field)
            if not isinstance(value, int) or value < 0:
                fail(f"{name}: {field} must be a nonnegative integer")
        for field in ("flashWriteDelayMs", "eepromWriteDelayMs", "chipEraseDelayMs",
                      "fuseWriteDelayMs", "lockWriteDelayMs", "resetDelayMs"):
            value = device.get(field)
            if not isinstance(value, int) or value < 0:
                fail(f"{name}: {field} must be a nonnegative integer")
        if device["flashSize"] and not device["flashPageSize"]:
            fail(f"{name}: nonempty flash requires a page size")
        if device["eepromSize"] and not device["eepromPageSize"]:
            fail(f"{name}: nonempty EEPROM requires a page size")
        if device["flashPageSize"] and device["flashSize"] % device["flashPageSize"] != 0:
            fail(f"{name}: flash size is not divisible by flash page size")
        if device["eepromPageSize"] and device["eepromSize"] % device["eepromPageSize"] != 0:
            fail(f"{name}: EEPROM size is not divisible by EEPROM page size")

        is_tpi = str(device.get("programmingInterface", "spi-isp")).casefold() == "tpi"
        if is_tpi:
            tpi_devices += 1
            if device["eepromSize"] != 0 or device["eepromPageSize"] != 0:
                fail(f"{name}: TPI device must not define EEPROM in this backend")
            for field in ("tpiFlashOffset", "tpiSignatureOffset", "tpiFuseOffset", "tpiLockOffset"):
                value = device.get(field)
                if not isinstance(value, int) or not 0 < value <= 0xFFFF:
                    fail(f"{name}: {field} must be a valid 16-bit TPI address")
        else:
            # Classic SPI-ISP AVR signatures encode the nominal Flash size in
            # the low nibble of signature byte 2: 1 KiB << nibble.
            signature_byte_2 = int(signature[2:4], 16)
            size_code = signature_byte_2 & 0x0F
            predicted_flash = 1 << (10 + size_code)
            if predicted_flash != device["flashSize"]:
                fail(
                    f"{name}: signature Flash-size verifier predicts {predicted_flash} bytes "
                    f"but metadata specifies {device['flashSize']} bytes"
                )
            flash_signature_matches += 1

            if size_code in (0, 1):
                predicted_page = 32
            elif size_code in (2, 3):
                predicted_page = 64
            elif size_code in (4, 5):
                predicted_page = 128
            elif size_code in (6, 7):
                predicted_page = 256
            else:
                predicted_page = 64
            if predicted_page == device["flashPageSize"]:
                page_signature_matches += 1
            else:
                page_signature_exceptions.append((name, predicted_page, device["flashPageSize"]))

        read_commands = device.get("fuseReadCommands", [])
        write_commands = device.get("fuseWriteCommands", [])
        read_masks = device.get("fuseReadMasks", [])
        program_masks = device.get("fuseProgramMasks", [])
        factory_values = device.get("fuseFactoryValues", [])
        if is_tpi:
            if len(read_commands) != 0 or len(write_commands) != 0:
                fail(f"{name}: TPI device must use memory-mapped fuse access, not SPI commands")
            if not (len(read_masks) == len(program_masks) == len(factory_values) == 1):
                fail(f"{name}: TPI device must define exactly one fuse mask/value")
        else:
            counts = {len(read_commands), len(write_commands), len(read_masks), len(program_masks), len(factory_values)}
            if len(counts) != 1:
                fail(f"{name}: fuse command and mask counts differ")
        if len(read_commands) > 3:
            fail(f"{name}: more than three fuse bytes are not supported")
        for command in [*read_commands, *write_commands]:
            if not isinstance(command, str) or not HEX8.fullmatch(command):
                fail(f"{name}: invalid four-byte fuse command {command!r}")
        for field_name, masks in (("fuseReadMasks", read_masks), ("fuseProgramMasks", program_masks),
                                  ("fuseFactoryValues", factory_values)):
            for mask in masks:
                if not isinstance(mask, int) or not 0 <= mask <= 0xFF:
                    fail(f"{name}: {field_name} contains an invalid byte")
        for fuse_index, (read_mask, program_mask) in enumerate(zip(read_masks, program_masks)):
            if program_mask & ~read_mask:
                fail(f"{name}: fuseProgramMasks[{fuse_index}] includes bits outside its read mask")

        for field in ("fuseReadPosition", "fuseWritePosition", "lockReadPosition", "lockWritePosition"):
            value = device.get(field)
            if not isinstance(value, int) or not 0 <= value <= 3:
                fail(f"{name}: {field} must be from 0 through 3")

        lock_read = device.get("lockReadCommand", "")
        lock_write = device.get("lockWriteCommand", "")
        if bool(lock_read) != bool(lock_write):
            fail(f"{name}: lock read/write command presence differs")
        for command in (lock_read, lock_write):
            if command and not HEX8.fullmatch(command):
                fail(f"{name}: invalid lock command {command!r}")
        lock_mask = device.get("lockProgramMask")
        if not isinstance(lock_mask, int) or not 0 <= lock_mask <= 0xFF:
            fail(f"{name}: invalid lockProgramMask")
        lock_factory = device.get("lockFactoryValue")
        if not isinstance(lock_factory, int) or not 0 <= lock_factory <= 0xFF:
            fail(f"{name}: invalid lockFactoryValue")
        if not lock_read and lock_mask != 0 and not is_tpi:
            fail(f"{name}: lockProgramMask must be zero when lock commands are absent")

    print(f"PASS: {len(devices)} device definitions validated.")
    print(
        f"PASS: SPI signature Flash-size verification matched "
        f"{flash_signature_matches}/{len(devices) - tpi_devices} SPI devices."
    )
    print(
        f"INFO: SPI signature page-size heuristic matched "
        f"{page_signature_matches}/{len(devices) - tpi_devices} SPI devices; "
        f"{len(page_signature_exceptions)} documented exceptions remain advisory."
    )
    for name, predicted, actual in page_signature_exceptions:
        print(f"  PAGE EXCEPTION: {name}: heuristic {predicted}, metadata {actual}")
    print(f"PASS: {tpi_devices} TPI device definitions validated.")
    aliases = {sig: part_names for sig, part_names in signatures.items() if len(part_names) > 1}
    if aliases:
        print(f"INFO: {len(aliases)} signatures have aliases or package variants:")
        for signature, part_names in sorted(aliases.items()):
            print(f"  {signature}: {', '.join(part_names)}")
    zero_masks = [
        device["name"]
        for device in devices
        if any(mask == 0 for mask in device.get("fuseProgramMasks", []))
    ]
    if zero_masks:
        print("INFO: fuse writing is intentionally disabled for ambiguous bytes on: "
              + ", ".join(zero_masks))
    zero_lock_masks = [
        device["name"]
        for device in devices
        if device.get("lockReadCommand") and device.get("lockProgramMask") == 0
    ]
    if zero_lock_masks:
        print("INFO: lock writing is intentionally disabled on: "
              + ", ".join(zero_lock_masks))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
