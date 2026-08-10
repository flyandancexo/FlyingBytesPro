<!-- Copyright (C) 2026 Flyandance JZ - GPL-3.0-or-later -->

# Sources and Provenance

## Current AVRDUDE configuration

- Project: AVRDUDE
- File: `third_party/avrdude/avrdude.conf.in`
- Role: primary mature implementation metadata for current `PM_ISP` parts
- License: GPL-2.0-or-later
- Upstream: https://github.com/avrdudes/avrdude/blob/main/src/avrdude.conf.in

AVRDUDE's part definitions provide signatures, programming modes, memory geometry, delays, commands, fuses, and lock memories. The generator resolves inherited part definitions before comparison.

## Microchip device packs

Exact pack versions listed in `data/microchip_pack_catalog.json`:

- Microchip.ATmega_DFP 3.6.299
- Microchip.ATtiny_DFP 3.4.278
- Microchip.ATautomotive_DFP 3.1.73

Official repository: https://packs.download.microchip.com/

The package intentionally does not redistribute these `.atpack` archives. `scripts/fetch_microchip_packs.ps1` downloads them from Microchip, and the generator reads their ATDF files.

## ProgISP 1.72

- Normalized interoperability reference: `reference/progisp172_avr_reference.json`
- Role: historical implementation cross-check
- No ProgISP executable or proprietary code is included.

## ATmega323 official supplement

- Official datasheet: https://ww1.microchip.com/downloads/en/DeviceDoc/doc1457.pdf
- Historical AVRDUDE signature reference: https://github.com/arduino/arduino-flash-tools/raw/refs/heads/master/tools_linux_64/avrdude/etc/avrdude.conf

## Generated source manifest

Hashes and exact source roles are recorded in `data/source_manifest.json`.
