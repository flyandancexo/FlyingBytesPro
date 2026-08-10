<!-- Copyright (C) 2026 Flyandance JZ - GPL-3.0-or-later -->

# Metadata Methodology

## Scope

The database targets devices programmable through the classic four-wire AVR ISP interface: RESET, MOSI, MISO, and SCK. It does not claim that UPDI, PDI, TPI, JTAG-only, debugWIRE-only, or high-voltage-only devices work through USBasp.

Records outside official AVR are retained only in separately labeled classes. Their presence in the master database does not make them supported by the Qt application.

## Inclusion rules

A normal master record is included when current AVRDUDE provides:

- A valid three-byte signature.
- `PM_ISP` among its programming modes.
- A positive Flash size.
- Enough command and memory metadata to describe the part.

A reviewed supplement may be included when a current AVRDUDE part block is absent, but an official datasheet and at least one independent implementation provide sufficient evidence. Such records must state their provenance and any disabled operations.

## Field selection

For Flash size, Flash page size, EEPROM size, EEPROM write granularity, signatures, commands, delays, fuse masks, and lock masks:

1. Official ATDF or datasheet values are preferred.
2. Current AVRDUDE values are selected when official machine-readable values are not present.
3. ProgISP is compared as a secondary implementation.
4. Signature heuristics are recorded as checks only.

## Conflict policy

- Official source versus selected implementation disagreement: critical; the record or affected write operation must be blocked until reviewed.
- AVRDUDE versus ProgISP disagreement: advisory; AVRDUDE remains selected unless official evidence indicates otherwise.
- Heuristic disagreement: expected exception; never a reason to replace sourced data.
- Missing fuse masks or unclear reserved bits: fuse writing disabled.
- Shared signatures: all aliases retained as distinct records; exact part selection required before configuration writes.

## Backend qualification

`support.currentUsbAspQtBackend` is separate from metadata quality.

A record is app-ready only when the current backend has compatible Flash page programming and EEPROM write metadata. Legacy byte-write Flash devices and devices with unknown EEPROM write granularity remain in the master list but are blocked from the current candidate file.

## Confidence labels

- `official-datasheet-selected`: reviewed official source controls the value.
- `avrdude-selected`: current AVRDUDE controls the value.
- `+progisp-agrees`: ProgISP independently matches critical geometry.
- `+progisp-disagrees`: disagreement is recorded for review.
- `hardwareTested`: a separate boolean, false unless a real part has been programmed and verified.

## Reproducibility

The generator resolves AVRDUDE part inheritance, parses instruction bit patterns into four-byte templates, compares ProgISP records by normalized name and signature, optionally parses Microchip ATDF files from `.atpack` archives, applies supplemental reviewed records, writes reports, and validates the result.
