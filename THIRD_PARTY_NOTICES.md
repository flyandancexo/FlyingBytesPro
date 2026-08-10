<!-- Copyright (C) 2026 Flyandance JZ - GPL-3.0-or-later -->

# Third-party notices

## Qt 6

FlyingBytesPro links to Qt 6 at build/runtime. Qt is supplied separately by the user's MSYS2 installation and is governed by its own licenses.

## libusb 1.0

FlyingBytesPro links to libusb 1.0. libusb is supplied separately by MSYS2 and is governed by its own license.

## Tabler Icons

Selected Tabler Icons SVG artwork was converted to PNG interface assets and embedded in the application. The original SVG files and exact MIT license are included under:

```text
resources\icon_sources\tabler\
```

Copyright (c) 2020-2026 Paweł Kuna.

## Protocol and metadata references

USBasp public documentation and mature open-source implementations were used as interoperability references. No AVRDUDE executable is required at runtime.

The embedded AVR definitions are normalized metadata. The accompanying metadata package documents AVRDUDE, ProgISP 1.72, signature heuristics, and Microchip device-pack work. ProgISP binaries and drivers are not redistributed in this source project.

## libwdi 1.5.1

The integrated USBasp driver window uses libwdi directly to prepare and install Microsoft WinUSB for supported USBasp devices. FlyingBytesPro bundles the prepared libwdi header and static library under `resources\libwdi`, along with the upstream libwdi license, version marker, and the exact upstream source snapshot pinned to commit `9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602`.

The bundled static library was prepared for WinUSB using Microsoft's WDK redistributables. Microsoft's redistribution terms apply to the embedded Microsoft driver-support components. Normal FlyingBytesPro builds do not download or rebuild libwdi.

The upstream libwdi license is included as `resources\libwdi\LICENSE.libwdi.txt` and is copied into the portable application during the Windows build.
