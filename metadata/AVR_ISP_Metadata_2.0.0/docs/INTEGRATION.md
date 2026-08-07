<!-- Copyright (C) 2026 Flyandance JZ - GPL-3.0-or-later -->

# Integration Guide

## Master schema

Use `data/avr_isp_devices.json` when a project can consume schema 2. It contains source provenance, programming interfaces, commands, geometry, timing, confidence, blockers, aliases, and verification results.

Use `data/avr_usbasp_qt_schema1_candidate.json` only for the existing AVR USBasp Studio schema. It contains 167 records that fit the current backend.

## Do not select by signature alone

Forty-nine signatures map to more than one named record. A safe API should return a list:

```cpp
QVector<const AvrDevice*> AvrDatabase::allBySignature(QByteArrayView signature) const;
```

The UI should then:

1. Show every matching part.
2. Explain that Flash geometry may be shared while fuse meanings can differ.
3. Permit Flash/EEPROM operations only after a record is selected.
4. Require an additional confirmation before fuse or lock writes in a shared-signature group.

## Recommended runtime checks

Before programming:

- Verify that image capacity does not exceed selected Flash or EEPROM size.
- Verify that Flash page size is positive and divides the Flash size.
- Compare signature-derived Flash capacity with database capacity.
- Log page-heuristic mismatch as an informational warning.
- Use long addressing for Flash above 64 KiB when supported by USBasp firmware.
- Respect the selected device's Flash page boundaries.
- Use EEPROM write granularity, not a guessed Flash-derived page size.

After programming:

- Read back all defined image bytes.
- Report the first mismatch and total mismatch count.
- Re-read fuses and lock bits after writes.

## Legacy backend work

Nineteen official AVR records have valid SPI-ISP metadata but are not in the current Qt candidate because the backend lacks one or both of:

- Legacy byte-write Flash support.
- Explicit byte-write EEPROM support when AVRDUDE does not define an EEPROM page size.

See `data/legacy_backend_gaps.csv`.
