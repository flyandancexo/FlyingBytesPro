<!-- Copyright (C) 2026 Flyandance JZ - GPL-3.0-or-later -->

# FlyingBytesProCLI V3.2.29

`FlyingBytesProCLI.exe` is the command-line interface for the same direct
libusb USBasp backend used by the Qt GUI. It does not launch AVRDUDE.

TPI-capable parts in the embedded database use the same `-p` workflow. The current TPI set is ATtiny4, ATtiny5, ATtiny9, ATtiny10, ATtiny20, ATtiny40, ATtiny102, and ATtiny104. The connected USBasp firmware must advertise TPI capability.

## Basic syntax

```text
FlyingBytesProCLI.exe -c usbasp -p <mcu-id> -U <memory>:<operation>:<file>[:format]
```

The `-U` form intentionally follows the common AVRDUDE memory-operation style.
Multiple `-U` operations are executed in command-line order.

## Common commands

Write and verify an Intel HEX Flash image:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U flash:w:firmware.hex:i
```

Write without automatic verification:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -V -U flash:w:firmware.hex:i
```

Write Flash without an automatic chip erase:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -D -U flash:w:firmware.hex:i
```

Read Flash to Intel HEX:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U flash:r:backup.hex:i
```

CLI Flash reads perform a complete physical Flash scan, then remove trailing erased `0xFF` bytes from the saved image extent. The GUI additionally offers Smart Read for faster application-only reads.

Read EEPROM to raw binary:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U eeprom:r:eeprom.bin:r
```

Verify a file without writing:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U flash:v:firmware.hex:i
```

Erase the MCU:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -e
```

Read fuses as text:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U lfuse:r:-:h -U hfuse:r:-:h
```

Write immediate fuse values:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U lfuse:w:0xE1:m -U hfuse:w:0x99:m
```

Read or write the lock byte:

```text
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U lock:r:-:h
FlyingBytesProCLI.exe -c usbasp -p atmega16 -U lock:w:0xFC:m
```

Detect the target and show the automatic-detection candidates sharing its signature. Redundant trailing-`A` aliases are omitted when the same-signature base MCU exists; those aliases remain available through explicit `-p` selection:

```text
FlyingBytesProCLI.exe -c usbasp -p auto --detect
```

List all 175 embedded MCU IDs (167 SPI ISP + 8 TPI):

```text
FlyingBytesProCLI.exe --list-parts
```

## Memories

- `flash`
- `eeprom`
- `lfuse` or `fuse`
- `hfuse`
- `efuse`
- `lock`
- `signature`

## Operations

- `r`: read from MCU
- `w`: write to MCU
- `v`: verify MCU against an input file or immediate value

## Formats

- `i`: Intel HEX
- `r`: raw binary
- `m`: immediate byte value, such as `0xE1`
- `h`: readable hexadecimal text
- `a`: choose from the file extension

Intel HEX is supported for Flash and EEPROM. Raw binary input starts at address
zero. Fuse and lock immediate values are one byte.

## SCK selection

With no `-B` or `--sck` option, the CLI uses **Pro** mode: USBasp SCK value `0`, matching the programmer firmware's default clock behavior.

`--sck auto` performs a real target scan from the fastest supported fixed clock downward until ISP entry succeeds. Fixed SCK values remain directly selectable. When the programmer must re-enter ISP after a chip erase or post-write session refresh, FlyingBytesPro reapplies the resolved SCK setting before reconnecting, matching the initialization order expected by USBasp firmware.

Use either form for a manual selection:

```text
FlyingBytesProCLI.exe -p atmega16 --sck 750k --signature
FlyingBytesProCLI.exe -p atmega16 -B 2.0 --signature
```

`-B` is the requested ISP bit-clock period in microseconds. It is mapped to the
fastest USBasp SCK that does not exceed the requested frequency.

## Safety behavior

- Flash writes automatically erase first unless `-D` is used or `-e` already
  performed the erase.
- Flash and EEPROM writes automatically verify unless `-V` is used.
- Every operation checks the selected MCU signature. An explicit `-p <mcu-id>` resolves a shared-signature target by that selected database record; ambiguity rejection is only used for automatic part selection.
- Automatic detection suppresses a trailing-`A` alias when the same-signature base MCU exists; remaining ambiguous signatures require an explicit `-p` selection.
- Fuse and lock writes preserve database-defined reserved bits and perform
  readback verification.
- `-n` validates the command, programmer, target, files, and values without
  erasing or writing the MCU.

## Compatibility scope

The command syntax is deliberately familiar to AVRDUDE users, but this is not
a complete AVRDUDE clone. FlyingBytesProCLI currently supports USBasp and the
classic AVR SPI-ISP memories implemented by FlyingBytesPro. It does not support
serial programmers, terminal mode, PDI, TPI, UPDI, JTAG, or arbitrary AVRDUDE
configuration files.


## License

FlyingBytesPro project code is licensed under GPL-3.0-or-later. See the root `LICENSE` file.
