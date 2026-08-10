#!/usr/bin/env python3
# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

"""Static packaging checks that do not require Qt or a Windows compiler."""

from __future__ import annotations

import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        fail(f"not a valid PNG file: {path.relative_to(ROOT)}")
    return struct.unpack(">II", data[16:24])


def require(text: str, terms: tuple[str, ...], label: str) -> None:
    for term in terms:
        if term not in text:
            fail(f"{label} is missing: {term}")


def main() -> int:
    required = (
        "CMakeLists.txt",
        "src/main.cpp",
        "src/cli/flyingbytespro_cli.cpp",
        "CLI_GUIDE.md",
        "src/gui/MainWindow.cpp",
        "src/gui/MainWindow.h",
        "src/gui/DisplayLanguage.cpp",
        "src/gui/DisplayLanguage.h",
        "src/gui/FuseLockDialog.cpp",
        "src/gui/FuseLockDialog.h",
        "src/gui/SckDial.cpp",
        "src/gui/UsbAspDriverDialog.cpp",
        "src/gui/UsbAspDriverDialog.h",
        "src/gui/MemoryMeterButton.cpp",
        "src/gui/ProgrammerController.cpp",
        "src/avr/AvrIspProgrammer.cpp",
        "src/avr/AvrIspProgrammer.h",
        "src/usb/UsbAspDevice.cpp",
        "src/usb/UsbAspDevice.h",
        "src/firmware/ProjectFile.cpp",
        "resources/resources.qrc",
        "resources/windows/app.ico",
        "resources/windows/app.rc.in",
        "resources/languages/zh-CN.json",
        "resources/languages/es.json",
        "resources/languages/ja.json",
        "resources/languages/de.json",
        "resources/languages/ko.json",
        "resources/languages/fr.json",
        "resources/languages/vi.json",
        "scripts/Prepare-Libwdi.ps1",
        "resources/libwdi/include/libwdi.h",
        "resources/libwdi/lib/libwdi.a",
        "resources/libwdi/LICENSE.libwdi.txt",
        "resources/libwdi/VERSION.txt",
        "resources/libwdi/libwdi-source.zip",
        "documentation/BUILD_WINDOWS_MSYS2.md",
        "documentation/TECHNICAL_README.md",
    )
    for item in required:
        if not (ROOT / item).is_file():
            fail(f"required file missing: {item}")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if "project(FlyingBytesPro" not in cmake or "VERSION 3.2.29" not in cmake:
        fail("CMake project name/version is incorrect")
    require(cmake, (
        "Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Widgets",
        "src/gui/UsbAspDriverDialog.cpp",
        "FLYINGBYTESPRO_LIBWDI_ROOT",
        "find_path(LIBWDI_INCLUDE_DIR",
        "find_library(LIBWDI_LIBRARY",
        '"${LIBWDI_LIBRARY}"',
        "setupapi newdev advapi32 crypt32 wintrust ole32 ntdll user32 shell32 version",
    ), "direct USBasp driver build integration")

    driver_dialog = (ROOT / "src/gui/UsbAspDriverDialog.cpp").read_text(encoding="utf-8")
    require(driver_dialog, (
        "setMinimumSize(520, 330);",
        "resize(540, 350);",
        'QStringLiteral(":/icons/FD-Logo.png")',
        'QStringLiteral(":/icons/usbasp_on.png")',
        "titleLabel->setAlignment(Qt::AlignCenter);",
        "subtitleLabel->setAlignment(Qt::AlignCenter);",
    ), "V3.2.29 compact branded USBasp driver header")
    if "setFrameShape(QFrame::HLine)" in driver_dialog:
        fail("V3.2.29 USBasp driver dialog still contains the removed separator line")
    if "Qt6::Network" in cmake or "Core Gui Widgets Network" in cmake:
        fail("retired Zadig-download Qt Network dependency remains")
    for source in re.findall(r"\b(?:src|resources)/[^\s\)]+", cmake):
        source = source.strip('"')
        if "${" in source or source.endswith("app.rc.in"):
            continue
        if not (ROOT / source).exists():
            fail(f"CMake references a missing path: {source}")

    tree = ET.parse(ROOT / "resources/resources.qrc")
    aliases: set[str] = set()
    for file_node in tree.findall(".//file"):
        text = (file_node.text or "").strip()
        if not text:
            fail("empty <file> entry in resources.qrc")
        resource_path = ROOT / "resources" / text
        if not resource_path.is_file():
            fail(f"resources.qrc references missing file: resources/{text}")
        alias = file_node.attrib.get("alias", text)
        if alias.casefold() in aliases:
            fail(f"duplicate resource alias: {alias}")
        aliases.add(alias.casefold())
    if tree.find('.//qresource[@prefix="/themes"]') is not None:
        fail("retired Flash theme resources are still embedded")

    expected_languages = {"zh-CN", "es", "ja", "de", "ko", "fr", "vi"}
    language_nodes = tree.findall('.//qresource[@prefix="/languages"]/file')
    language_aliases = {Path(node.attrib.get("alias", "")).stem for node in language_nodes}
    if language_aliases != expected_languages:
        fail(f"language resource set is incorrect: {sorted(language_aliases)}")
    fuse_ui_document = json.loads((ROOT / "resources/devices/fuse_ui.json").read_text(encoding="utf-8"))
    decoder_strings = set()
    def add_decoder_string(value):
        if not isinstance(value, str) or not value.strip():
            return
        normalized = re.sub(r"\s+", " ", value).strip()
        normalized = re.sub(r"Ext\. RC Osc\. - 0\.9 MHz",
                            "Ext. RC Osc. 0.1 MHz - 0.9 MHz",
                            normalized, flags=re.IGNORECASE)
        decoder_strings.add(normalized)
    for device in fuse_ui_document.get("devices", []):
        for section in device.get("fuses", []):
            for bit in section.get("bits", []):
                add_decoder_string(bit.get("description"))
            for option in section.get("options", []):
                add_decoder_string(option.get("text"))
                add_decoder_string(option.get("description"))
        lock = device.get("lock", {})
        for bit in lock.get("bits", []):
            add_decoder_string(bit.get("description"))
        for option in lock.get("options", []):
            add_decoder_string(option.get("text"))
            add_decoder_string(option.get("description"))
        for warning in device.get("warnings", []):
            if isinstance(warning, str):
                add_decoder_string(warning)
            elif isinstance(warning, dict):
                for warning_value in warning.values():
                    add_decoder_string(warning_value)
    if len(decoder_strings) != 825:
        fail(f"unexpected fuse-decoder canonical string count: {len(decoder_strings)}")

    placeholder = re.compile(r"%[1-9]")
    language_key_set = None
    for code in sorted(expected_languages):
        pack_path = ROOT / f"resources/languages/{code}.json"
        pack = json.loads(pack_path.read_text(encoding="utf-8"))
        strings = pack.get("strings", {})
        if pack.get("language") != code or len(strings) != 1200:
            fail(f"language pack is incomplete or has unexpected key count: {code}: {len(strings)}")
        current_keys = set(strings)
        if language_key_set is None:
            language_key_set = current_keys
        elif current_keys != language_key_set:
            fail(f"language pack canonical key set differs: {code}")
        for source, target in strings.items():
            if not source or not isinstance(target, str) or not target:
                fail(f"language pack contains an empty entry: {code}: {source!r}")
            if sorted(placeholder.findall(source)) != sorted(placeholder.findall(target)):
                fail(f"language placeholder mismatch: {code}: {source}")
            if re.search(r"%[1-9]%[1-9]", source) or re.search(r"%[1-9]%[1-9]", target):
                fail(f"adjacent placeholders are unsafe for translated template reordering: {code}: {source}")
        missing_decoder = decoder_strings.difference(strings)
        if missing_decoder:
            fail(f"fuse-decoder translations are missing in {code}: {len(missing_decoder)}")
        if strings.get("%1 — Not Available") is None:
            fail(f"localized fuse-decoder unavailable suffix is missing: {code}")
        if strings.get("Flash") != "Flash" or strings.get("EEPROM") != "EEPROM":
            fail(f"Flash/EEPROM technical names must remain canonical English: {code}")
        for source, target in strings.items():
            if any(term in target for term in ("闪存", "EEPROM存储", "フラッシュ", "플래시")):
                fail(f"translated Flash/EEPROM technical term remains: {code}: {source}")
        required_read_crc = {
            "%1 bytes Flash read at %2 in %3 — %4 bytes used — CRC 0x%5",
            "%1 bytes EEPROM read at %2 in %3 — CRC 0x%4",
        }
        if not required_read_crc.issubset(strings):
            fail(f"localized read+CRC templates are missing: {code}")
        required_driver_ui = {
            "Install Driver",
            "USBasp",
            "USBasp Driver Installation",
            "USBasp Device:",
            "Driver installation failed: %1",
        }
        if not required_driver_ui.issubset(strings):
            fail(f"localized WinUSB driver UI strings are missing: {code}")
        if code in {"zh-CN", "ja"}:
            cjk = r"[\u3040-\u30ff\u3400-\u9fff]"
            for source, target in strings.items():
                if re.search(rf"[A-Za-z]{cjk}|{cjk}[A-Za-z]", target):
                    fail(f"Latin/CJK text requires a separating space: {code}: {source}: {target}")
        if code == "zh-CN" and strings.get("Flash and EEPROM Memories") != "Flash 与 EEPROM":
            fail("Simplified Chinese Flash/EEPROM group title spacing is incorrect")
        if code == "ja" and strings.get("Flash and EEPROM Memories") != "Flash と EEPROM":
            fail("Japanese Flash/EEPROM group title spacing is incorrect")
        required_task_summary = {
            "[Flash %1 B written + verified]",
            "[Flash written + verified]",
            "[EEPROM %1 B written + verified]",
            "[EEPROM written + verified]",
        }
        if not required_task_summary.issubset(strings):
            fail(f"localized merged task-summary templates are missing: {code}")
        required_log_operations = {
            "Blank Check EEPROM", "Blank Check Flash", "Read Fuses", "Write Fuses",
            "Automatic Task Sequence: %1",
        }
        if not required_log_operations.issubset(strings):
            fail(f"localized full-log operation labels are missing: {code}")
        if code == "zh-CN":
            expected_zh = {
                "Write Flash": "编程 Flash",
                "Write EEPROM": "编程 EEPROM",
                "%1 bytes Flash written at %2 in %3": "Flash 已编程 %1 字节，速度 %2，耗时 %3",
                "%1 bytes EEPROM written at %2 in %3": "EEPROM 已编程 %1 字节，速度 %2，耗时 %3",
                "[Flash %1 B written + verified]": "[Flash 已编程 %1 B + 已校验]",
                "[EEPROM %1 B written + verified]": "[EEPROM 已编程 %1 B + 已校验]",
            }
            for source, expected in expected_zh.items():
                if strings.get(source) != expected:
                    fail(f"Simplified Chinese programming terminology regressed: {source}")
            expected_zh_driver = {
                "Driver:": "驱动：",
                "Install Driver": "安装驱动",
                "Microsoft WinUSB — %1 — Recommended": "Microsoft WinUSB — %1 — 推荐",
                "Windows system driver": "Windows 系统驱动",
            }
            for source, expected in expected_zh_driver.items():
                if strings.get(source) != expected:
                    fail(f"Simplified Chinese driver terminology regressed: {source}")
            for traditional in ("驅動", "選擇", "權限", "繼續", "無法", "當前", "僅", "連接"):  # selected high-risk forms
                for source in required_driver_ui:
                    if traditional in strings[source]:
                        fail(f"Traditional Chinese form remains in zh-CN driver UI: {source}: {strings[source]}")
        decoder_english_glue = {
            "zh-CN": r"\b(?:is|are|the|and|or|when|through|during|default value|section size|start address|memory|enabled|disabled|programming|verification|clock|source|oscillator|startup|time|preserved|chip erase|using|select|enable|disable|will|make|interface|power|unless|with|than|more|data downloading|serial|load capacitors|between)\b",
            "ja": r"\b(?:is|are|the|and|or|when|through|during|default value|section size|start address|memory|enabled|disabled|programming|verification|clock|source|oscillator|startup|time|preserved|chip erase|using|select|enable|disable|will|make|interface|power|unless|with|than|more|data downloading|serial|load capacitors|between)\b",
            "ko": r"\b(?:is|are|the|and|or|when|through|during|default value|section size|start address|memory|enabled|disabled|programming|verification|clock|source|oscillator|startup|time|preserved|chip erase|using|select|enable|disable|will|make|interface|power|unless|with|than|more|data downloading|serial|load capacitors|between)\b",
            "es": r"\b(?:default value|section size|start address|memory is|is enabled|is disabled|chip erase|through the|during through|Preserve )\b",
            "de": r"\b(?:default value|section size|start address|memory is|is enabled|is disabled|chip erase|through the|during through|Preserve )\b",
            "fr": r"\b(?:default value|section size|start address|memory is|is enabled|is disabled|chip erase|through the|during through|Preserve )\b",
            "vi": r"\b(?:default value|section size|start address|memory is|is enabled|is disabled|chip erase|through the|during through|Preserve )\b",
        }
        glue_pattern = decoder_english_glue[code]
        for source in decoder_strings:
            if re.search(glue_pattern, strings[source], flags=re.IGNORECASE):
                fail(f"mixed-English fuse decoder translation remains: {code}: {source}: {strings[source]}")

    for size in (16, 24, 32, 48, 64, 128, 256):
        path = ROOT / f"resources/icons/app_{size}.png"
        if png_size(path) != (size, size):
            fail(f"incorrect application icon dimensions: {path.name}")

    for name in (
        "usbasp_on.png", "usbasp_off.png", "detect_target.png",
        "load.png", "save.png", "load_eeprom.png", "save_eeprom.png",
        "project_load_purple.png", "project_save_pink.png",
        "write.png", "read.png", "start.png", "Button_Start.png",
        "project_load.png", "full_log.png", "settings.png", "about.png",
        "chevron_down.png", "lock_gold.png", "FD-Logo.png", "Pigeon_Coffee.png",
        "MCU_Search_B.png", "MCU_Search.png",
        "Mem-Flash_B.png", "Mem-Flash_B_off.png", "Mem-Flash_B_light_only.png",
        "Mem-EEPROM_B.png", "Mem-EEPROM_B_off.png", "Mem-EEPROM_B_light_only.png",
        "clock_sck.png", "Fuse_High.png", "Fuse_Low.png", "Fuse_Lock.png",
    ):
        width, height = png_size(ROOT / "resources/icons" / name)
        if width < 16 or height < 16:
            fail(f"UI icon is too small: {name}")

    expected_artwork = {
        "usbasp_off.png": "5593e7792c085c173fc1586225703c3fc7c34a30acbdfe20af17c31ce715bf72",
        "usbasp_on.png": "a7ecfdbdcb293d86d01478b023b86c807c640804b517965df9498799787b94a8",
        "Fuse_High.png": "eddc0f9a28cfa56977e96d916ba2fa8d19989a28ca06ee41a1ca93d7b03fb73f",
        "Fuse_Low.png": "8a32e81e7b476bfe6f7e949082c5972f494c8d93b97149a28a7e779a13378991",
        "Fuse_Lock.png": "108e3bd9efd2133d71a94db6515b90fb1e2a66894d548e632205ae83098a6879",
        "Button_Start.png": "907e3499f8dadf0b587460e8281bce311c3d2fca95f31af4d3522a846b0316c7",
        "Mem-Flash_B.png": "1095d591a82271920703c66ea44cf609866c8883474e369166b2b9b1b5cecc27",
        "Mem-Flash_B_off.png": "d0b3b853d872bf8eafdc4bcb1b2b6c7c72f4bf565d9c9bd2b6b64c895b94d69a",
        "Mem-Flash_B_light_only.png": "1c2e63d48a855bf0aa49c75eb09cec99aae1e582e0dff25b7331eeaed27c5f81",
        "Mem-EEPROM_B.png": "935e3608fe4fc04b9e4aa8bd1d6bef962ef1b6849d84c9ca024f7c6d545f32a2",
        "Mem-EEPROM_B_off.png": "a3eba00a8b7a9b2ca45448f8cfe5ec5a7a8519fd99dbf0cd1b2dfcea4fa8b8b1",
        "Mem-EEPROM_B_light_only.png": "196dbc5f6d66253d5bf8b42144617374651754a6a2f9e92caa903ed5199e28d7",
        "MCU_Search.png": "d15116000a7076c3bdd8d781e0de96840f2c53205adeeda6c99abba71e591575",
        "Pigeon_Coffee.png": "55be9524c18b68471d6cf6762a28af863a8527e1ea4e96cd70a04599d0738697",
        "load_eeprom.png": "03b60d8eaf196d3b8c6292bb25de9e19b2a7accb08b2d6541051507542d9ce51",
        "save_eeprom.png": "2a4d3d4126b0905c965ce468fab9844b9b32ae76eb96fef067f0a147899d2d4a",
        "project_save_pink.png": "504b88b238d4dc042554c7341c4040e9a9d006b875926f3a2554a976c65d9629",
    }
    for name, expected in expected_artwork.items():
        actual = hashlib.sha256((ROOT / "resources/icons" / name).read_bytes()).hexdigest()
        if actual != expected:
            fail(f"validated artwork hash is incorrect: {name}")

    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    require(readme, (
        "# FlyingBytesPro V3.2.28",
        "### A full-featured programmer for classic AVR",
        "## One application for the complete AVR programming workflow",
        "## Task — build a complete programming job and run it once",
        "## Fuse & Lock — configure the MCU with context, not guesswork",
        "## Flash Memory — inspect, edit, read, write, and understand the layout",
        "## EEPROM Memory — edit data safely and program by real device boundaries",
        "## Full Log — detailed visibility when hardware needs an explanation",
        "## GUI and CLI — use whichever fits the job",
        "## USBasp Driver — install WinUSB directly from FlyingBytesPro",
        "Settings > Install Driver > USBasp",
        "Microsoft **WinUSB**",
        "resources/libwdi",
        "## Built for real USBasp behavior",
        "## Highlights",
        "## Buy Me a Coffee",
        "GPL-3.0-or-later",
    ), "showcase README")
    technical = (ROOT / "documentation" / "TECHNICAL_README.md").read_text(encoding="utf-8")
    require(technical, (
        "# FlyingBytesPro V3.2.29 — Technical Reference and Development History",
        "The next development version is **V3.2.30**.",
        "### Display-language layer",
        "resources/languages/",
        "functional identifiers or data",
        "## USBasp transport behavior",
        "## AVR ISP programming behavior",
        "## Automatic programming sequence",
        "## Validation and hardware evidence",
        "# Consolidated legacy implementation history",
        "## Legacy 0.3.25",
    ), "consolidated technical reference")
    if list(ROOT.glob("CHANGELOG_*.md")):
        fail("per-version changelog files remain after documentation consolidation")

    tests_cpp = (ROOT / "tests/test_firmware.cpp").read_text(encoding="utf-8")
    require(tests_cpp, (
        "void legacyProjectTaskMigration();",
        "source.taskSelections = {true, true, false, true, false, true, false, false, false};",
        "const QVector<bool> expected = {true, true, false, true, false, true, false, false, false};",
    ), "current 9-task project round trip and legacy 10-task migration regression")
    require(tests_cpp, (
        "void highestDefinedAddressTracksSparseEnd();",
        "QCOMPARE(image.highestDefinedAddress(), qsizetype(1023));",
    ), "sparse verification-boundary regression")
    require(tests_cpp, (
        "void cleanTrailingErasedReadExtent();",
        "image.cleanTrailingErased(128)",
        "QCOMPARE(image.definedCount(), qsizetype(32));",
        "QCOMPARE(blank.definedCount(), qsizetype(0));",
    ), "Flash-read erased-tail cleanup regression")
    require(tests_cpp, (
        "void legacyProjectClockMigration();",
        'root.insert(QStringLiteral("schemaVersion"), 1);',
        'root.insert(QStringLiteral("clockId"), 0);',
        "usbasp::IspClock::Auto",
    ), "legacy V3.2.6 Auto clock migration regression")
    project_file = (ROOT / "src/firmware/ProjectFile.cpp").read_text(encoding="utf-8")
    require(project_file, (
        'root.insert(QStringLiteral("schemaVersion"), 2);',
        "schemaVersion != 1 && schemaVersion != 2",
        "schemaVersion == 1 && loaded.clockId == 0",
        "usbasp::IspClock::Auto",
        "FlyingBytesPro V3.2.29",
    ), "project schema-2 clock migration")

    database = json.loads((ROOT / "resources/devices/avr_devices.json").read_text(encoding="utf-8"))
    devices = database.get("devices", [])
    if len(devices) != 175:
        fail("embedded candidate must contain 175 device records")
    tpi_devices = [device for device in devices if device.get("programmingInterface") == "tpi"]
    if len(tpi_devices) != 8:
        fail("embedded candidate must contain exactly 8 TPI device records")

    main_cpp = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    if "FlyingBytesPro" not in main_cpp or "V3.2.29" not in main_cpp:
        fail("application identity is missing from src/main.cpp")

    main_window = (ROOT / "src/gui/MainWindow.cpp").read_text(encoding="utf-8")
    programmer_controller = (ROOT / "src/gui/ProgrammerController.cpp").read_text(encoding="utf-8")
    bad_programmer_spelling = "Program" + "mor"
    if bad_programmer_spelling in main_window:
        fail("misspelled programmer status remains in MainWindow.cpp")
    require(main_window, (
        "FlyingBytesPro V3.2.29", "Task", "Flash", "EEPROM", "Full Log",
        "Settings", "About", "Log", "Save Project", "Load Project",
        ":/icons/MCU_Search_B.png", ":/icons/Mem-Flash_B.png",
        ":/icons/Mem-Flash_B_off.png", ":/icons/Mem-Flash_B_light_only.png",
        ":/icons/Mem-EEPROM_B.png", ":/icons/Mem-EEPROM_B_off.png",
        ":/icons/Mem-EEPROM_B_light_only.png", ":/icons/Button_Start.png",
        ":/icons/FD-Logo.png",
        "m_startTaskButton->setFixedSize(170, 46)",
        "m_startTaskButton->setIconSize(QSize(164, 39))",
        "leftLayout->setSpacing(5)",
        "leftLayout->setAlignment(Qt::AlignTop)",
        "const int alignedTaskColumnHeight = rightLayout->sizeHint().height()",
        "leftColumn->setFixedHeight(alignedTaskColumnHeight)",
        "rightColumn->setFixedHeight(alignedTaskColumnHeight)",
        "0.20 + 0.80 * 0.5",
        "m_memoryActivityTimer->setInterval(40)",
        "memoryActivityFrame(bool flash, qreal lightOpacity)",
        "startMemoryActivity(flash)",
        "stopMemoryActivity()",
        ":/icons/Pigeon_Coffee.png", "Buy Me a Coffee",
        "Supports 175 AVR microcontrollers: 167 through SPI ISP plus 8 through TPI.",
        "Sophisticated and beautiful GUI with a simple, focused design.",
        "New AVRDUDE-style command-line interface using the USBasp protocol.",
        "Auto SCK scans from the fastest supported clock downward; Pro uses the USBasp firmware default clock request",
        "Smart and Full MCU Flash reads with automatic erased-tail cleanup.",
        "Open-source software licensed under GNU GPL-3.0-or-later.",
        "by Flyandance JZ from San Francisco, 2026",
        "Write Final Fuses",
        'm_taskChecks[3], nullptr, QStringLiteral("ATmega"))',
        'm_taskChecks[4], nullptr, QStringLiteral("ATtiny")))',
        'm_taskChecks[5], nullptr, QStringLiteral("ATmega"))',
        'm_taskChecks[6], nullptr, QStringLiteral("ATtiny")))',
        'layout->addStretch();',
        'QStringLiteral(":/icons/lock_gold.png")',
        'const QString eepromFileButtonStyle = QStringLiteral(',
        'QPushButton:hover { background: #eaf2fb; border-color: #6d9fd0; }',
        'setBusy(true, QStringLiteral("Detecting Target..."), false)',
        'QStringLiteral("taskSelections")',
        'QStringLiteral("fuseValues/%1")',
        "restoreTaskSelections()",
        "restoreFuseValuesForDevice(*device)",
        "saveFuseValuesForDeviceId(device->id)",
        'setMinimumSize(1230, 650);',
        'resize(1230, 780);',
        'windowLayoutSchema < 4 && width() < 1230',
        'QStringLiteral("mainWindowLayoutSchema"), 4',
        'memoryBlock->setFixedWidth(ui.table->width());',
        'centeredRow->addWidget(memoryBlock, 0);',
        'table->horizontalHeader()->resizeSection(0, 96);',
        'table->horizontalHeader()->resizeSection(column, 34);',
        'table->horizontalHeader()->resizeSection(17, 96);',
        'table->horizontalHeader()->resizeSection(18, 160);',
        'table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);',
        'table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);',
        'const int memoryTableWidth = table->horizontalHeader()->length()',
        '+ table->verticalScrollBar()->sizeHint().width()',
        '+ (2 * table->frameWidth());',
        'table->setFixedWidth(memoryTableWidth);',
        'ui.bootInfoLabel = new QLabel(memoryBlock);',
        'bootSectionMetadataForDevice(*device)',
        'bootSectionSupportText(*device, metadata)',
        'm_flashUi.model->setBootloaderHighlight(true, startByte)',
        'return QStringLiteral("<b>Bootloader:</b> %1").arg(items.join(QLatin1Char(\' \')));',
        'ui.bootInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter)',
        'ui.bootInfoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred)',
        'ui.bootInfoLabel->setMinimumWidth(0)',
        'ui.bootInfoLabel->setText(QStringLiteral("<b>Bootloader:</b> n/a"))',
        'ui.bootInfoLabel->setTextFormat(Qt::RichText)',
        'editRow->addSpacing(51)',
        'QTabBar::tab { background: #e8edf2; border: 1px solid #bcc5cf;',
        'border-top-left-radius: 6px;',
        'border-top-right-radius: 6px;',
        'padding: 7px 17px;',
        'min-width: 76px;',
        'max-width: 76px;',
        'margin-right: 6px;',
        'QTabWidget::tab-bar { left: 16px; }',
        'QTabBar::tab:hover { background: #f2f6fa; }',
        'font-weight: 700; background: #ffffff;',
        'padding-top: 5px; padding-left: 16px; padding-right: 16px;',
        'color: #142b3d;',
        'selectedTabBorderColorForIndex(int index)',
        'case 0: return QStringLiteral("#FF9100")',
        'case 1: return QStringLiteral("#469BD7")',
        'case 2: return QStringLiteral("#48AA62")',
        'case 3: return QStringLiteral("#8967CA")',
        'case 4: return QStringLiteral("#BEBEBE")',
        'case 5: return QStringLiteral("#C6A47E")',
        'border-left: 2px solid %1',
        'border-top: 3px solid %1',
        'border-top-left-radius: 6px; border-top-right-radius: 6px;',
        'padding-left: 16px; padding-right: 16px;',
        'border-right: 2px solid %1',
    ), "requested V3.2.28 Fixed_V7 UI behavior")
    if 'QIcon(QStringLiteral(":/icons/start.png"))' in main_window:
        fail("Fixed_V7 Task tab still has the V3.2.28 added activity icon")
    if 'QProxyStyle' in main_window or 'sizeFromContents(ContentsType type' in main_window:
        fail("Fixed_V7 must not replace the native/style-sheet tab geometry")
    if 'painter->fillPath(path, fill)' in main_window or 'label.palette.setColor' in main_window:
        fail("Fixed_V7 tabs still apply custom fill/text colors")
    require(main_window, (
        '"<b>ID:</b> [%1]<br>"',
        'QStringLiteral("<b>ID:</b> [-- -- --]<br>"',
        'layout->addWidget(languageBox);',
        'QStringLiteral("Restore Defaults")',
        'QStringLiteral("Restore Default Settings")',
        'QStringLiteral("Settings restored to defaults.")',
        'QStringLiteral("Language: %1")',
        'new QGroupBox(QStringLiteral("Install Driver"), page)',
        'new QPushButton(QStringLiteral("USBasp"), driverBox)',
        'UsbAspDriverDialog dialog(this);',
    ), "V3.2.28 MCU ID, Settings defaults/language, driver tool, and compact language status")
    require(main_window, (
        'const QStringList sourceTabNames{',
        'tabFont.setBold(true);',
        'const QFontMetrics metrics(tabFont);',
        'constexpr int fixedTabContentWidth = 76;',
        'availableTextWidth -= iconWidth + iconTextGap;',
        'metrics.horizontalAdvance(translated)',
        '> availableTextWidth ? english : translated',
    ), "translated tab labels use the real fixed tab text area before English fallback")
    if 'Display language changed to %1. Programming logic' in main_window:
        fail("verbose language-change runtime message remains")
    if 'setStretchLastSection(true)' in main_window:
        fail("memory tables still contain the reverted V3.2.17 full-width stretch behavior")
    if 'setMinimumWidth(930)' in main_window or 'setMaximumWidth(1040)' in main_window:
        fail("memory table still uses the oversized V3.2.18 width range that leaves a gap before the scrollbar")
    signature_pos = main_window.find('layout->addWidget(signatureBox);')
    language_pos = main_window.find('layout->addWidget(languageBox);', signature_pos)
    driver_pos = main_window.find('layout->addWidget(driverBox);', language_pos)
    restore_pos = main_window.find('layout->addWidget(restoreBox);', driver_pos)
    stretch_pos = main_window.find('layout->addStretch();', restore_pos)
    if (signature_pos < 0 or language_pos < 0 or restore_pos < 0 or driver_pos < 0 or stretch_pos < 0
            or not (signature_pos < language_pos < driver_pos < restore_pos < stretch_pos)):
        fail("USBasp Driver must appear above Restore Defaults before the trailing Settings stretch")

    hex_table = (ROOT / "src/gui/HexTableModel.cpp").read_text(encoding="utf-8")
    require(hex_table, (
        "if (index.column() == kAsciiColumn)",
        "if (section == kAsciiColumn)",
        "return static_cast<int>(Qt::AlignCenter);",
    ), "center-aligned ASCII memory column")
    require(hex_table, (
        "const bool addressCell = index.column() == kAddressColumn",
        "|| index.column() == kDecimalAddressColumn;",
        "if (addressCell && bootRow)",
        "QColor(218, 237, 252)",
        "QColor(234, 246, 255)",
    ), "address-only alternating blue bootloader highlight")
    if "QColor(255, 232, 199)" in hex_table or "QColor(255, 242, 220)" in hex_table:
        fail("retired orange full-row bootloader highlighting remains")

    avr_database = (ROOT / "src/avr/AvrDatabase.cpp").read_text(encoding="utf-8")
    require(avr_database, (
        "naturalNameCompare",
        "atmegaSortParts",
        "static constexpr quint64 groups[] = {256, 128, 88, 64, 48, 32, 16, 8};",
        "leftMega.modelDigits.compare",
        "std::stable_sort(loaded.begin(), loaded.end(), naturalDeviceLess);",
        "AvrDatabase::bySignatureDetectionCandidates",
        "candidate->name.endsWith(QLatin1Char('A'), Qt::CaseInsensitive)",
        "const QString baseName = candidate->name.left(candidate->name.size() - 1);",
    ), "natural MCU ordering and trailing-A detection alias policy")
    require(main_window, (
        "m_database.bySignatureDetectionCandidates(signature)",
    ), "GUI automatic-detection candidate policy")

    require(main_window, (
        'QStringLiteral("Clear all")',
        'QStringLiteral("Clear Selected")',
        'QStringLiteral("Zero all")',
        'QStringLiteral("Zero Selected")',
        'model->clearBuffer()',
        'model->clearOffsets(offsets)',
        'model->fill(0xFF, true)',
        'model->fillOffsets(offsets, 0xFF, true)',
        'QPushButton { color: #CD5C5C; font-weight: 700; }',
        'QPushButton { color: #1766B5; font-weight: 700; }',
        'm_clearLogButton->setVisible(index != fullLogTabIndex)',
    ), "memory-editor controls and Full Log bottom-Clear visibility")
    forbidden_task_tooltips = (
        'm_clearLogButton->setToolTip',
        'm_miniLog->setToolTip',
        'Open Pre-Write Fuse Controls.',
        'Open Fuse Controls.',
        'Open Lock-Fuse Controls.',
        'Flash memory operation',
        'EEPROM memory operation',
        'Flash memory verification',
        'EEPROM memory verification',
    )
    for token in forbidden_task_tooltips:
        if token in main_window:
            fail(f"Task-page tooltip noise remains: {token}")
    if 'setMiniLog(QStringLiteral("MCU selected:' in main_window:
        fail("manual MCU selection must not create a Task-tab Log entry")
    if "m_tabs->setCurrentIndex" in main_window or "m_tabs->setCurrentWidget" in main_window:
        fail("operations must never force a tab switch")
    if "this, &MainWindow::appendLog" not in main_window:
        fail("controller low-level logs must be routed to Full Log")
    if 'settings.value(QStringLiteral("confirmMemoryWrites"), false)' not in main_window:
        fail("direct memory-write confirmation must default to off")
    if 'settings.value(QStringLiteral("verifyAfterWrite"), false)' not in main_window:
        fail("write verification must default to off")
    require(main_window, (
        'QStringLiteral("Write Options")',
        'QStringLiteral("Confirm Before Writing Flash or EEPROM")',
        'QStringLiteral("Verify Flash or EEPROM After Writing")',
        'QStringLiteral("Flash Read")',
        'QStringLiteral("Smart Read")',
        'QStringLiteral("Full MCU Read")',
        'QStringLiteral("flashReadMode"), QStringLiteral("smart")',
        'QStringLiteral("MCU Signature Check")',
        'QStringLiteral("Use Selected MCU Without Checking Its Signature")',
        'settings.value(QStringLiteral("ignoreMcuSignatureMatching"), false)',
        'settings.setValue(QStringLiteral("ignoreMcuSignatureMatching"), enabled)',
        'Signature matching skipped by the Settings option.',
    ), "concise settings and persistent optional signature matching bypass")
    for retired_setting_text in (
        "Ask for Confirmation Before Direct Flash or EEPROM Writes",
        "Verify Flash or EEPROM After Direct Write",
        "Write confirmation and automatic read-back verification are independent.",
        "When enabled, the selected MCU definition is used without reading or comparing",
    ):
        if retired_setting_text in main_window:
            fail(f"retired Settings explanation remains: {retired_setting_text}")
    require(main_window, (
        ':/icons/MCU_Search.png',
        'm_checkMcuButton = makeImageButton',
        'm_signatureCheckOnly = true;',
        'MCU signature matched: %2 — ID: [%1]',
        'MCU signature mismatch: read [%1], selected %2 — ID: [%3].',
        'm_tabs->addTab(m_flashUi.page, mcuClassIcon(QStringLiteral("ATmega"))',
        'm_tabs->addTab(m_eepromUi.page, mcuClassIcon(QStringLiteral("ATtiny"))',
        "<b>ID:</b> [%1]<br>",
        "color:#287bb8",
        "color:#2d9b57",
        'ProgressState::Flash',
        'ProgressState::Eeprom',
        'ProgressState::Error',
        'QStringLiteral("#7fc8f2")',
        'QStringLiteral("#8dd8ad")',
        'QStringLiteral("#CD5C5C")',
        'QStringLiteral("#ffffff")',
        'if (m_progressState != ProgressState::Flash',
        '&& m_progressState != ProgressState::Eeprom) return;',
        'if (m_activeTask == TaskStep::ProgramFlash)',
        'else if (m_activeTask == TaskStep::ProgramEeprom)',
        'startProgress(ProgressState::Idle);',
    ), "MCU controls and Flash/EEPROM-only progress tracking")
    if 'ProgressState::OtherSuccess' in main_window:
        fail("retired GoldenRod success progress state remains")
    require(main_window, (
        'QStringLiteral("A-Okay ☆☆☆")',
        'QStringLiteral("Error! ★★★")',
        'QStringLiteral("Programmer Online :)")',
        'QStringLiteral("Programmer Offline :(")',
        'onlineText.remove(QStringLiteral(" :)"))',
        'offlineText.remove(QStringLiteral(" :("))',
        'appendLog(QStringLiteral("A-Okay ☆☆☆ %1").arg(onlineText))',
        'appendLog(QStringLiteral("A-Okay ☆☆☆ %1").arg(offlineText))',
        'completeProgressResult(success)',
    ), "Fixed_V9 result markers and USBasp presence wording")
    require(main_window, (
        '"<b>ID:</b> [%1]<br>"',
        'QStringLiteral("MCU detected: %1 — ID: [%2]")',
        'QStringLiteral("%1 loaded: %2 bytes — CRC 0x%3")',
        'QStringLiteral("Project loaded — %1 — Flash CRC 0x%2")',
        'QStringLiteral("%1 — CRC 0x%2")',
        'QStringLiteral("Fuses read — Low %2, High %1, Extended %3, Lock %4")',
        'QStringLiteral("^(\\\\d+) bytes (Flash|EEPROM) written")',
    ), "Fixed_V11 compact MCU-ID and mini-log wording")
    require(main_window, (
        "table->setFocusPolicy(Qt::ClickFocus);",
        "table->setCurrentIndex(QModelIndex());",
        "table->selectionModel()->selectedIndexes().isEmpty()",
        "QTimer::singleShot(0, table",
    ), "Fixed_V9 memory tables do not auto-focus/select the top-left cell")
    require(main_window, (
        'matches multiple AVR devices.\\nSelect the exact MCU:',
        'QStringLiteral("===== Automatic Task Sequence: %1 =====")',
        'const bool separator = displayMessage.startsWith(QStringLiteral("===== "))',
        'const bool technical = displayMessage.startsWith(QStringLiteral("USB CTRL"))',
        'displayMessage.prepend(QStringLiteral("  "))',
        'm_log->setStyleSheet(QStringLiteral("QPlainTextEdit { color: #000000; }"))',
        'format.setForeground(QColor(QStringLiteral("#000000")))',
    ), "Fixed_V9 black normal log presentation, shared-MCU prompt, and readable Full Log grouping")
    for retired in (
        'OKAY! ☆☆☆', 'ERROR ★★★', 'A-Okay ★★★', 'Error! ☆☆☆', '#89CFF0',
        'bootInfoFont.setBold(true)'
    ):
        if retired in main_window:
            fail(f"retired Fixed_V7 presentation remains: {retired}")
    require(programmer_controller, (
        'Q_EMIT logMessage(QStringLiteral("===== %1 =====").arg(name));',
        'beginOperation(QStringLiteral("Verify Flash"));',
        'beginOperation(QStringLiteral("Erase Chip"));',
        'beginOperation(QStringLiteral("Blank Check Flash"));',
        'beginOperation(QStringLiteral("Blank Check EEPROM"));',
        'beginOperation(QStringLiteral("Read Fuses"));',
        'beginOperation(QStringLiteral("Write Fuses"));',
        'beginOperation(QStringLiteral("Write Lock Fuse"));',
    ), "Fixed_V7 Full Log main-event separators and readable operation names")
    require(main_window, (
        ':/icons/load_eeprom.png',
        ':/icons/save_eeprom.png',
        ':/icons/project_load_purple.png',
        ':/icons/project_save_pink.png',
        'color: #1766b5; font-weight: 700;',
        'color: #228B22; font-weight: 700;',
        'color: #B8860B; font-weight: 700;',
        'color: #8a5cc2; font-weight: 700;',
        'color: #FFA07A; font-weight: 700;',
        'new HexTableModel(flash, ui.page)',
    ), "color-coded Files controls and memory-specific hexadecimal models")
    require(main_window, (
        'QDesktopServices::openUrl',
        'https://paypal.me/flyandance?country.x=US&locale.x=en_US',
        'https://github.com/flyandancexo/FlyingBytesPro',
        'Open FlyingBytesPro on GitHub',
        'contentLayout->setSpacing(100);',
        'if (i == 1) {',
        'm_taskChecks[1]->isChecked()',
        '(m_taskChecks[3]->isChecked() && flashHasData)',
        'm_taskQueue.append(TaskStep::EraseChip);',
        'm_taskElapsedTimer.start();',
        'm_taskElapsedTimer.elapsed()',
        'summary = QStringLiteral("%1 completed in %2.")',
        'taskSummaryItem(m_activeTask, message)',
        "m_taskSummaryItems.join(QLatin1Char(\' \'))",
        'setTaskMiniLogLine(QString(), true);',
        "setTaskMiniLogLine(m_taskSummaryItems.join(QLatin1Char(' ')), true);",
        'setTaskMiniLogLine(summary, success);',
        'm_taskMiniLogBlockNumber = cursor.block().blockNumber();',
        'm_taskQueueIndex + 1 < m_taskQueue.size()',
        'QStringLiteral("selectedDeviceId")',
        'QStringLiteral("sckClockId")',
        'QStringLiteral("sckClockSchema")',
        'usbasp::IspClock::Pro',
        'usbasp::IspClock::Auto',
        'm_deviceCombo->findData(QStringLiteral("atmega88"))',
        'connect(m_sckDial, &QDial::valueChanged',
    ), "donation About layout, concise task summary, persistent MCU/SCK, and single implied Program Flash erase")
    if 'm_taskChecks[1]->setEnabled(false)' in main_window \
            or 'm_taskChecks[1]->setEnabled(!enabled)' in main_window:
        fail("Erase Chip must remain enabled while Program Flash is selected")
    if 'm_taskChecks[1]->setChecked(true);' in main_window:
        fail("Program Flash must imply erase in the queue without forcing the checkbox")
    if "Automatic USBasp detection started" in main_window:
        fail("automatic USBasp detection startup must not create a log entry")
    if 'setMiniLog(QStringLiteral("%1 — %2")' in main_window:
        fail("automatic task steps must not create individual Task-page log lines")
    if 'm_taskSummaryItems.clear();\n  if (m_miniLog) m_miniLog->clear();' in main_window:
        fail("automatic Task mini-log history must not be cleared at sequence start")
    if 'Automatic programming:' in main_window:
        fail("retired Automatic programming prefix remains in Task summary")

    if "Erased-Chip Check" in main_window or "TaskStep::BlankCheck" in main_window:
        fail("retired Erased-Chip Check automatic task remains")
    require(main_window, (
        'color: #FFA07A; font-weight: 700;',
        'saved.remove(3, 1);',
    ), "LightSalmon Save Project text and legacy task-selection migration")
    meter_source = (ROOT / "src/gui/MemoryMeterButton.cpp").read_text(encoding="utf-8")
    require(meter_source, (
        "QColor(232, 234, 237)",
        "emptyBorder(231, 233, 236)",
    ), "faint empty memory-meter presentation")

    usbasp_source = (ROOT / "src/usb/UsbAspDevice.cpp").read_text(encoding="utf-8")
    require(usbasp_source, (
        'QStringLiteral("Programmer not found.")',
        'QStringLiteral("Target MCU not detected.")',
    ), "simplified programmer and target errors")
    if "No USBasp was found. Expected VID:PID" in usbasp_source:
        fail("retired verbose programmer-not-found message remains")
    license_text = (ROOT / "LICENSE").read_text(encoding="utf-8")
    if "GNU GENERAL PUBLIC LICENSE" not in license_text or "Version 3, 29 June 2007" not in license_text:
        fail("root LICENSE is not GNU GPL version 3")

    shared_dialog_start = main_window.find("const AvrDevice* MainWindow::chooseSharedDevice")
    shared_dialog_end = main_window.find("void MainWindow::startTaskSequence", shared_dialog_start)
    shared_dialog = main_window[shared_dialog_start:shared_dialog_end]
    if ':/icons/MCU_Search.png' not in shared_dialog:
        fail("shared-signature selection dialog is missing MCU_Search.png")
    if 'QStringLiteral("Cancel")' in shared_dialog:
        fail("shared-signature selection dialog still has a separate Cancel button")

    for retired_fuse_gate in (
        "m_fuseReadDeviceId",
        "Read the current fuse values from this exact MCU before writing.",
        "Read the current lock value from this exact MCU before writing.",
        "Read the current fuses and lock byte from this exact",
        "Fuse and lock values must be read again before another fuse or lock write.",
    ):
        if retired_fuse_gate in main_window:
            fail(f"retired manual fuse/lock pre-read gate remains: {retired_fuse_gate}")
    if 'QMessageBox::warning(&dialog, QStringLiteral("Write Fuses")' in main_window:
        fail("fuse-write confirmation warning remains")
    if (
        'QMessageBox::warning(&dialog, QStringLiteral("Write Lock Fuse")' not in main_window
        and 'QMessageBox::warning(&dialog, DisplayLanguage::text(QStringLiteral("Write Lock Fuse"))' not in main_window
        and 'showLocalizedMessageBox(&dialog, QMessageBox::Warning,' not in main_window
    ):
        fail("lock-fuse warning must remain")
    require(main_window, (
        "chooseSharedDevice", "new QPushButton(mcuClassIcon(device->name)",
        "lastFileDirectory()", "rememberFileDirectory(path)",
        "memoryBox->setMinimumWidth(300);",
        "if (ui.model->image().definedCount() == 0)",
        "const bool flashHasData = m_flashUi.model->image().definedCount() > 0;",
        "const bool eepromHasData = m_eepromUi.model->image().definedCount() > 0;",
        "(m_taskChecks[3]->isChecked() && flashHasData)",
    ), "shared-signature, remembered-folder, empty-buffer no-op, and memory-button clearance")
    if "imageForWrite" in main_window or "The empty %1 buffer is being programmed as 0x00" in main_window:
        fail("retired empty-buffer 0x00 synthesis behavior remains")
    if "Intel HEX Text" in main_window or "Clear Undefined" in main_window:
        fail("retired memory-editor controls remain")
    if 'QStringLiteral("Operation Failed")' in main_window:
        fail("routine operation failures must not open modal windows")
    require(main_window, (
        'QStringLiteral("Write Flash to MCU")',
        'QStringLiteral("Write EEPROM to MCU")',
        'QStringLiteral("ATtiny")',
        'void removeImageButtonOutline(QToolButton* button)',
        'QToolButton { border: none; border-radius: 8px; background: transparent; padding: 0px; }',
        'QToolButton:hover { border: none; background: rgba(43, 125, 183, 24); }',
        'QToolButton:pressed { border: none; background: rgba(43, 125, 183, 48); padding-left: 2px; padding-top: 2px; }',
        'removeImageButtonOutline(m_usbaspButton)',
        'QPushButton, QToolButton { border: 1px solid #aeb6c1;',
        'widget->setAttribute(Qt::WA_TransparentForMouseEvents, busy);',
    ), "custom write controls, borderless image controls, and ordinary button outlines")
    if 'QStringLiteral("Auto MCU")' in main_window:
        fail("retired Auto MCU text remains")
    require(main_window, (
        'm_startTaskButton->setFocusPolicy(Qt::NoFocus);',
        'QPushButton:hover { border: none; background: rgba(43, 125, 183, 24); }',
        'QPushButton:pressed { border: none; background: rgba(43, 125, 183, 48); padding-left: 2px; padding-top: 2px; }',
        'button->setFocusPolicy(Qt::NoFocus);',
    ), "focus-free Start/file controls and Start interaction feedback")
    if 'QStringLiteral("Start Programming Sequence")' in main_window:
        fail("automatic task sequence confirmation dialog remains")
    if 'Run these tasks on %1 in order?' in main_window:
        fail("retired automatic task confirmation message remains")

    require(main_window, (
        'clockLayout->setContentsMargins(1, 1, 1, 4);',
        'memoryLayout->setContentsMargins(6, 5, 6, 7);',
    ), "three-pixel top-strip control offset")
    if 'widget->setEnabled(!busy);' in main_window:
        fail("busy-state operation controls must not be disabled and visually flashed")

    sck = (ROOT / "src/gui/SckDial.cpp").read_text(encoding="utf-8")
    require(sck, (
        ':/icons/clock_sck.png',
        'constexpr qreal kStartDegrees = 214.0;',
        'constexpr qreal kEndDegrees = -34.0;',
        'faceRect.top() + faceRect.height() * 0.574',
        'const qreal innerRadius = faceRect.height() * 0.315;',
        'const qreal outerRadius = faceRect.height() * 0.395;',
        'markerPen.setCapStyle(Qt::RoundCap);',
        'painter.drawLine(barInner, barOuter);',
        'Qt::AlignCenter | Qt::TextSingleLine',
        'const qreal textWidth = faceRect.width() * 0.58;',
        'const qreal textHeight = faceRect.height() * 0.25;',
        'centerFont.setPixelSize(std::max(11, qRound(faceRect.height() * 0.135)));',
        'setValueFromPosition(event->position());',
        'void SckDial::mouseMoveEvent(QMouseEvent* event)',
        'QColor(24, 164, 98)',
        'QColor(218, 165, 32)',
        'QColor(221, 54, 48)',
        'QStringLiteral("AUTO")',
        'QStringLiteral("PRO")',
        'usbasp::IspClock::Auto',
        'usbasp::IspClock::Pro',
        'setFocusPolicy(Qt::NoFocus);',
    ), "full-range SCK clock face, radial bar, and one-line value")
    if ('painter.drawEllipse(marker' in sck or 'if (hasFocus())' in sck
            or 'painter.drawRoundedRect(textRect' in sck
            or "displayText.replace" in sck):
        fail("retired SCK dot, focus rectangle, bordered label, or split text remains")

    meter = (ROOT / "src/gui/MemoryMeterButton.cpp").read_text(encoding="utf-8")
    require(meter, (
        "QLinearGradient", "Bytes %4%", "fillRect.width() * fraction",
        "QColor(191, 226, 255)", "QColor(187, 239, 215)",
        "QColor(232, 234, 237)", "emptyBackground(246, 247, 248)",
    ), "compact memory meter")
    if "Bytes |" in meter or "0x%5" in meter:
        fail("memory meters must not display separators or checksum text")
    if ":/themes/" in meter or "flashFrame" in meter or "flashFill" in meter:
        fail("retired textured Flash meter code remains")
    if "CLICK TO READ" in meter:
        fail("memory meters must not display Click to Read text")
    if 'QStringLiteral("Read %1 from MCU")' not in meter:
        fail("memory meter MCU read tooltip is missing")

    programmer = (ROOT / "src/avr/AvrIspProgrammer.cpp").read_text(encoding="utf-8")
    require(programmer, (
        "if (clock == usbasp::IspClock::Pro)",
        "m_usbasp.setIspClock(usbasp::IspClock::Pro",
        "Pro SCK using USBasp default.",
        "if (clock != usbasp::IspClock::Auto)",
        "attempts.append(usbasp::IspClock::MHz3);",
        "attempts.append(usbasp::IspClock::MHz1_5);",
        "attempts.append(usbasp::IspClock::Hz500);",
        "Auto SCK selected %1.",
        "Target MCU not detected at any automatic SCK setting.",
    ), "Pro default SCK and true Auto scan behavior")
    for retired_auto in (
        "Auto SCK using USBasp default.",
        "const usbasp::IspClock requestedClock = clock == usbasp::IspClock::Auto",
    ):
        if retired_auto in programmer:
            fail(f"retired V3.2.6 Auto behavior remains: {retired_auto}")
    programmer_header = (ROOT / "src/avr/AvrIspProgrammer.h").read_text(encoding="utf-8")
    require(programmer_header, (
        "bool ignoreSignatureMatching = false",
        "bool m_ignoreSignatureMatching = false",
        "bool m_refreshSessionBeforeNextDeviceAccess = false",
    ), "signature-bypass and post-write session-recovery programmer interface")
    require(programmer, (
        "m_ignoreSignatureMatching(ignoreSignatureMatching)",
        "if (m_ignoreSignatureMatching)",
        "error.clear();",
        "if (m_refreshSessionBeforeNextDeviceAccess)",
        "reenterProgrammingMode(device.resetDelayMs, error)",
        "m_refreshSessionBeforeNextDeviceAccess = totalBytes > 0;",
        "m_refreshSessionBeforeNextDeviceAccess = false;",
        "if (image.definedCount() == 0)",
        "if (!m_usbasp.setIspClock(m_clock, clockError)",
        "m_clock != usbasp::IspClock::Pro",
    ), "signature bypass, empty-write no-op, SCK reapply, and post-write ISP-session recovery")
    if programmer.count("m_refreshSessionBeforeNextDeviceAccess = totalBytes > 0;") != 2:
        fail("Flash and EEPROM writes must both request a clean ISP session before the next device access")

    flash_write = programmer[programmer.index("bool AvrIspProgrammer::writeFlash"):
                             programmer.index("bool AvrIspProgrammer::writeEeprom")]
    if not (flash_write.index("if (image.definedCount() == 0)")
            < flash_write.index("validateDevice(device, error)")
            < flash_write.index("chipErase(device, error)")):
        fail("empty Flash write must return before signature validation and chip erase")
    eeprom_write = programmer[programmer.index("bool AvrIspProgrammer::writeEeprom"):
                              programmer.index("bool AvrIspProgrammer::readFuses")]
    if not (eeprom_write.index("if (image.definedCount() == 0)")
            < eeprom_write.index("validateDevice(device, error)")):
        fail("empty EEPROM write must return before signature validation")
    reenter = programmer[programmer.index("bool AvrIspProgrammer::reenterProgrammingMode"):
                         programmer.index("bool AvrIspProgrammer::execute")]
    if not (reenter.index("disconnectTarget()")
            < reenter.index("setIspClock(m_clock")
            < reenter.index("connectTarget(error)")
            < reenter.index("enableProgramming(error)")):
        fail("SPI ISP re-entry must reapply SCK before reconnect and program-enable")

    controller = (ROOT / "src/gui/ProgrammerController.cpp").read_text(encoding="utf-8")
    if bad_programmer_spelling in controller:
        fail("misspelled programmer status remains in ProgrammerController.cpp")
    require(controller, (
        "ignoreMcuSignatureMatchingSetting",
        'QStringLiteral("ignoreMcuSignatureMatching")',
        "AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());",
        "transferRateText", "elapsedTimeText",
        "%1 bytes Flash read at %2 in %3",
        "%1 bytes EEPROM read at %2 in %3",
        "%1 bytes Flash written at %2 in %3",
        "%1 bytes Flash written at %2 in %3 — verified, %4 total",
        "%1 bytes EEPROM written at %2 in %3",
        "%1 bytes EEPROM written at %2 in %3 — verified, %4 total",
        "programmer.lastTransferBytes()",
        "programmer.lastTransferElapsedMs()",
        "QElapsedTimer operationTimer",
        "m_progressEmitTimer.elapsed() >= 40",
        'QStringLiteral("Programmer Online :)")',
        'QStringLiteral("%1s")',
        "KB/s", "B/s",
    ), "optimized transfer-rate and progress reporting")
    require(controller, (
        "%1 bytes Flash read at %2 in %3 — %4 bytes used",
        "%1 bytes EEPROM read at %2 in %3",
        "%1 bytes Flash written at %2 in %3",
        "%1 bytes Flash written at %2 in %3 — verified, %4 total",
        "%1 bytes EEPROM written at %2 in %3",
        "%1 bytes EEPROM written at %2 in %3 — verified, %4 total",
        'QStringLiteral("Flash verified.")',
        'QStringLiteral("EEPROM verified.")',
        'QStringLiteral("Chip erased.")',
        'QStringLiteral("Fuses written + verified.")',
        'QStringLiteral("Lock byte written + verified.")',
    ), "Fixed_V11 concise and translation-safe mini-log operation messages")

    usbasp = (ROOT / "src/usb/UsbAspDevice.cpp").read_text(encoding="utf-8")
    require(usbasp, (
        "USBasp bulk read started",
        "USBasp bulk read completed",
        "USBasp bulk write started",
        "USBasp bulk write completed",
        "m_detailedBulkTracing",
        "traceSuccessfulTransfer",
        "memoryTransferTimeoutMs",
        "* 128 * 1000",
    ), "optimized USBasp bulk transfer tracing and low-SCK timeouts")
    protocol = (ROOT / "src/usb/UsbAspProtocol.h").read_text(encoding="utf-8")
    require(protocol, (
        "inline constexpr int ReadBlockSize = 200;",
        "inline constexpr int WriteBlockSize = 200;",
        "inline constexpr int QueueSafeFlashWriteBlockSize = 128;",
        "inline constexpr int SlowReadBlockSize = 8;",
        "inline constexpr int SlowWriteBlockSize = 8;",
        "Pro = 0",
        "Auto = 14",
    ), "USBasp protocol block limits and Pro/Auto clock IDs")
    require(protocol, (
        "FuncTpiConnect = 11",
        "FuncTpiDisconnect = 12",
        "FuncTpiRawRead = 13",
        "FuncTpiRawWrite = 14",
        "FuncTpiReadBlock = 15",
        "FuncTpiWriteBlock = 16",
        "TpiBlockSize = 32",
        "CapabilityTpi",
    ), "USBasp TPI protocol definitions")
    require(usbasp, (
        "connectTpi", "enableTpiProgramming", "tpiReadMemory",
        "tpiWriteMemory", "tpiChipErase", "tpiWriteConfigByte",
        "usbasp::TpiBlockSize",
    ), "USBasp TPI transport")
    require(programmer, (
        "beginTpi", "usbasp::CapabilityTpi", "device.isTpi()", "device.tpiFlashOffset",
        "device.tpiFuseOffset", "device.tpiLockOffset",
        "const qsizetype highestDefined = image.highestDefinedAddress();",
        "const qsizetype verifyLength = highestDefined + 1;",
        "const qsizetype pageSize = std::max<qsizetype>(1, device.flashPageSize);",
        "if (allErased(page))",
        "image.cleanTrailingErased(bytes.size());",
    ), "TPI programmer path, bounded verification reads, and Smart/Full Flash reads")
    require(main_window, (
        'format.setForeground(QColor(QStringLiteral("#CD5C5C")))',
        "m_taskChecks[4]->setEnabled(hasEeprom)",
        "m_taskChecks[6]->setEnabled(hasEeprom)",
    ), "IndianRed error logs and TPI EEPROM task suppression")
    require(usbasp, (
        "const bool isolateEepromPages = functionId == usbasp::FuncWriteEeprom;",
        "const bool queueSafeFlashBlocks = functionId == usbasp::FuncWriteFlash;",
        "usbasp::QueueSafeFlashWriteBlockSize",
        "usbasp::SlowReadBlockSize",
        "usbasp::SlowWriteBlockSize",
        "std::min<qsizetype>(protocolMaximumBlock, pageSize)",
        "const qsizetype bytesToPageBoundary = isolateEepromPages",
        "bytesToBoundary, bytesToPageBoundary",
        "blockFlags = isolateEepromPages ? usbasp::BlockFlagFirst : 0;",
        "if (!setLongAddress(currentAddress, error))",
        "const bool requiresLongAddress = address >= 0x10000u;",
        "if (!requiresLongAddress && !m_longAddressActive)",
        "const quint32 requestedAddress = requiresLongAddress ? address : 0u;",
        "m_longAddressActive = requiresLongAddress;",
        "usbasp::FuncSetLongAddress",
    ), "queue-safe Flash writes, page-isolated EEPROM writes, and required-only long addressing")
    for retired in (
        "LongAddressSupport::Unsupported",
        "LongAddressSupport::Supported",
        "maximumAttempts",
        "probed once without retry delays",
    ):
        if retired in usbasp:
            fail(f"retired 0.3.20 long-address probe logic remains: {retired}")

    require(programmer, (
        "QVector<quint8> current;",
        "if (!readFuses(device, current, error))",
        "if (!readLock(device, current, error))",
        "struct MemoryRun",
        "appendRunPage",
        "runBytes",
        "pageFullyDefined",
        "m_lastTransferBytes = totalBytes",
    ), "aggregated Flash and EEPROM write path")

    model = (ROOT / "src/gui/HexTableModel.cpp").read_text(encoding="utf-8")
    require(model, (
        "constexpr int kBytesPerRow = 16",
        "constexpr int kDecimalAddressColumn = 17",
        "constexpr int kAsciiColumn = 18",
        "return parent.isValid() ? 0 : 19",
        'QStringLiteral("HEX Address")',
        'QStringLiteral("DEC Address")',
        "return QString::number(rowBase)",
        "Edit printable ASCII directly",
        "section == kAsciiColumn",
        "m_flash ? QColor(20, 70, 190) : QColor(45, 155, 87)",
        "void HexTableModel::clearBuffer()",
        "void HexTableModel::clearOffsets(const QList<qsizetype>& offsets)",
        "void HexTableModel::fillOffsets(const QList<qsizetype>& offsets",
        'm_undo.beginMacro(DisplayLanguage::text(QStringLiteral("Clear selected bytes")))',
        'QStringLiteral("Zero selected bytes")',
        'QStringLiteral("Fill buffer with FF")',
        'QStringLiteral("Clear buffer")',
        "const int lastRow = static_cast<int>((offset + count - 1) / kBytesPerRow);",
        "void HexTableModel::setBootloaderHighlight",
        "m_bootloaderHighlightEnabled",
        "const bool addressCell = index.column() == kAddressColumn",
        "|| index.column() == kDecimalAddressColumn;",
        "QColor(218, 237, 252)",
        "QColor(234, 246, 255)",
    ), "byte editor with dual addresses and undoable Clear/Zero operations")
    fill_block = model[model.index("void HexTableModel::fill("):
                       model.index("void HexTableModel::clearBuffer()")]
    if "m_undo.clear()" in fill_block or "m_undo.setClean()" in fill_block:
        fail("Fill with FF must preserve the existing undo/redo history")

    cli = (ROOT / "src/cli/flyingbytespro_cli.cpp").read_text(encoding="utf-8")
    require(cli, (
        "FlyingBytesProCLI V3.2.29",
        "database.bySignatureDetectionCandidates(signature)",
        "parseUpdateSpec",
        "-U <spec>",
        "usbasp::IspClock::MHz3",
        "usbasp::IspClock::Pro",
        "usbasp::IspClock::Auto",
        'value == QStringLiteral("pro")',
        'value == QStringLiteral("auto")',
        "programmer.writeFlash",
        "programmer.readFlash",
        "programmer.verifyFlash",
        "programmer.writeEeprom",
        "programmer.readEeprom",
        "programmer.writeFuses",
        "programmer.writeLock",
        "options.disableAutoErase",
        "options.disableVerify",
        "options.dryRun",
        "MCU ID\\tName\\tInterface\\tSignature\\tFlash\\tEEPROM",
        'device.isTpi() ? QStringLiteral("TPI") : QStringLiteral("SPI ISP")',
    ), "command-line programmer")
    if "else if (automaticPart && matches.size() > 1)" not in cli:
        fail("CLI shared-signature ambiguity must be restricted to automatic part selection")
    if "AVRDUDE" in cli and "does not launch AVRDUDE" not in (ROOT / "CLI_GUIDE.md").read_text(encoding="utf-8"):
        fail("CLI documentation must state that AVRDUDE is not launched")
    if "add_executable(FlyingBytesProCLI" not in cmake:
        fail("CMake does not build FlyingBytesProCLI")
    if "FlyingBytesProCLI.exe" not in (ROOT / "scripts/Build-And-Run.ps1").read_text(encoding="utf-8"):
        fail("build script does not package FlyingBytesProCLI.exe")

    build_script = (ROOT / "scripts/Build-And-Run.ps1").read_text(encoding="utf-8")
    require(build_script, (
        "scripts\\Prepare-Libwdi.ps1",
        "resources\\libwdi",
        "documentation",
        "-DFLYINGBYTESPRO_LIBWDI_ROOT=$LibwdiRoot",
        "LICENSE.*.txt",
    ), "one-click bundled USBasp WinUSB dependency check")
    for forbidden in ("LibusbKRuntime", "runtime\\libusbK.dll", "libusbK.dll"):
        if forbidden in build_script:
            fail(f"alternate-driver runtime remains in WinUSB-only build script: {forbidden}")
    if "explorer.exe" in build_script:
        fail("build script must not open the portable output folder")
    if (ROOT / "CMakePresets.json").exists():
        fail("unused CMakePresets.json remains in the source root")
    if "CMakePresets.json" in build_script or "--preset" in build_script:
        fail("build script still references the removed CMake presets")
    for forbidden in ("$SourceDocDir", "$DistDocDir", "Copy-Item -LiteralPath $SourceDocDir"):
        if forbidden in build_script:
            fail(f"developer documentation is still copied into dist: {forbidden}")
    if "Developer documentation is source-only" not in build_script:
        fail("build script does not enforce source-only developer documentation")

    app_rc = (ROOT / "resources/windows/app.rc.in").read_text(encoding="utf-8")
    if "FILEVERSION 3,2,29,0" not in app_rc or '"V3.2.29\\0"' not in app_rc:
        fail("Windows version metadata is incorrect")

    print("PASS: static project audit completed.")
    print("PASS: 175 embedded device records found.")
    print(f"PASS: {len(aliases)} Qt resource aliases resolved, including 7 display-language packs.")
    fuse_ui = json.loads((ROOT / "resources/devices/fuse_ui.json").read_text(encoding="utf-8"))
    if len(fuse_ui.get("devices", [])) < 100:
        fail("fuse UI metadata coverage is unexpectedly small")
    fuse_ui_text = json.dumps(fuse_ui, ensure_ascii=False)
    if "Ext. RC Osc.         -  0.9 MHz" in fuse_ui_text:
        fail("legacy external-RC text with a missing lower bound remains")
    if "Ext. RC Osc. 0.1 MHz - 0.9 MHz" not in fuse_ui_text:
        fail("corrected external-RC 0.1-0.9 MHz range is missing")

    main_window = (ROOT / "src/gui/MainWindow.cpp").read_text(encoding="utf-8")
    require(main_window, (
        "QJsonObject exactNameUnknownSignature",
        "const QByteArray unknownSignature(3, '\\0')",
        "if (exactName && signature == device.signature) return object;",
        "if (exactName && signature == unknownSignature",
        "if (!exactNameUnknownSignature.isEmpty()) return exactNameUnknownSignature;",
    ), "fuse metadata exact-name/unknown-signature lookup")

    embedded_devices = json.loads(
        (ROOT / "resources/devices/avr_devices.json").read_text(encoding="utf-8")
    ).get("devices", [])
    metadata_devices = fuse_ui.get("devices", [])

    def matched_fuse_metadata(device):
        exact_unknown = None
        signature_fallback = None
        target_name = str(device.get("name", "")).casefold()
        target_signature = str(device.get("signature", "")).upper()
        for metadata in metadata_devices:
            metadata_name = str(metadata.get("name", "")).casefold()
            metadata_signature = str(metadata.get("signature", "")).upper()
            exact_name = metadata_name == target_name
            if exact_name and metadata_signature == target_signature:
                return metadata
            if exact_name and metadata_signature == "000000" and exact_unknown is None:
                exact_unknown = metadata
            if metadata_signature == target_signature and signature_fallback is None:
                signature_fallback = metadata
        return exact_unknown if exact_unknown is not None else signature_fallback

    decoded_classic = 0
    for device in embedded_devices:
        if device.get("programmingInterface", "spi") == "tpi":
            continue
        if matched_fuse_metadata(device) is not None:
            decoded_classic += 1
    if decoded_classic < 119:
        fail(f"fuse decoder runtime coverage regressed to {decoded_classic} classic devices")

    for required_name in (
        "ATmega328P", "ATmega16U4", "AT90USB162", "ATmega88P",
        "ATtiny48", "ATtiny88",
    ):
        device = next((item for item in embedded_devices
                       if item.get("name") == required_name), None)
        if device is None or matched_fuse_metadata(device) is None:
            fail(f"fuse decoder metadata is not reachable for {required_name}")

    fuse_dialog = (ROOT / "src/gui/FuseLockDialog.cpp").read_text(encoding="utf-8")
    require(fuse_dialog, (
        "Fuses", "Lock Fuse", "Fuse Bits", "Lock Bits", "Decoded Settings",
        "QStringLiteral(\"Read\")", "QStringLiteral(\"Default\")",
        "QStringLiteral(\"Write\")",
        "const QVector<int> fuseOrder{HighByte, LowByte, ExtendedByte}",
        ':/icons/Fuse_High.png',
        ':/icons/Fuse_Low.png',
        ':/icons/Fuse_Lock.png',
        '#3EB489', '#1766B5', '#CD5C5C',
        "writeFusesRequested(this->fuseValues())",
        "writeLockRequested(this->lockValue())",
        "byteData.toInt(&byteIndexOk)",
        "applyOption(item, item->checkState(0) == Qt::Checked)",
        "button->setText(rawOne ? QStringLiteral(\"1\") : QStringLiteral(\"0\"))",
        "QLineEdit::textEdited",
        'QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{0,2}$"))',
        'stateButton->setProperty("writable", hardwareWritable)',
        "if (!readable || !writable)",
        "edit->deselect()",
        "kByteColumnWidth = 142",
        "kFuseSectionWidth",
        "kLockSectionWidth",
        "lockBox->setFixedWidth(kLockSectionWidth)",
        "fuseLayout->addWidget(m_valueEdits[byteIndex], 1, column",
        "Qt::AlignRight | Qt::AlignVCenter",
        "Qt::AlignLeft | Qt::AlignVCenter",
        "lockLayout->setContentsMargins(kSectionInnerMargin + 10, 7,",
        "lockLayout->setHorizontalSpacing(15);",
        "lockLayout->setColumnMinimumWidth(0, 59)",
        "lockLayout->setColumnMinimumWidth(1, 60)",
        "lockLayout->setColumnStretch(2, 1)",
        "lockImage->setFixedSize(42, 43)",
        "lockLayout->addWidget(m_valueEdits[LockByte], 1, 0, Qt::AlignCenter)",
        "readFusesButton->setStyleSheet(readButtonStyle)",
        "readLockButton->setStyleSheet(readButtonStyle)",
        "m_writeFusesButton->setStyleSheet(writeButtonStyle)",
        "m_writeLockButton->setStyleSheet(writeButtonStyle)",
        "normalizeOptionText",
        "Ext. RC Osc. 0.1 MHz - 0.9 MHz",
        "setMinimumSize(650, 480)",
        'QStringLiteral("%1 - %2")',
        "m_device.name",
    ), "compact Fuse and Lock dialog")
    if "#B19CD9" in fuse_dialog:
        fail("retired purple fuse Read color remains in FuseLockDialog.cpp")
    if "▲" in fuse_dialog or "isCriticalBit" in fuse_dialog:
        fail("Fuse and Lock labels must not contain triangle suffixes")
    for retired in ("Read All", "Factory Default", "Write Fuses", "Write Lock", "OK", "Cancel"):
        if f"QStringLiteral(\"{retired}\")" in fuse_dialog:
            fail(f"retired Fuse and Lock button remains: {retired}")
    if "verticalDivider" in fuse_dialog or "QStringLiteral(\"\\u26A0\")" in fuse_dialog:
        fail("Fuse and Lock dialog must not contain divider lines or warning-icon widgets")
    if "QStringLiteral(\"CKSEL0\")" in fuse_dialog or "QStringLiteral(\"CKSEL3\")" in fuse_dialog:
        fail("clock-selection bits must not be marked as critical")
    if "item->data(0, Qt::UserRole).toInt(-1)" in fuse_dialog:
        fail("FuseLockDialog must not pass an integer default to QVariant::toInt")
    if 'QStringLiteral("0x%1")' in fuse_dialog:
        fail("Fuse text fields must not display a 0x prefix")
    if "font.setBold(true)" not in model or model.count("return static_cast<int>(Qt::AlignCenter);") < 5:
        fail("memory editor headers must be bold and the ASCII header/data must be center aligned")
    require(model, (
        "constexpr int kDecimalAddressColumn = 17;",
        "constexpr int kAsciiColumn = 18;",
        "return parent.isValid() ? 0 : 19;",
        'QStringLiteral("HEX Address")',
        'QStringLiteral("DEC Address")',
        "return QString::number(rowBase);",
        "void HexTableModel::fillOffsets",
        'QStringLiteral("Zero selected bytes")',
    ), "dual-address memory table and selected-byte zero operation")
    driver_dialog = (ROOT / "src/gui/UsbAspDriverDialog.cpp").read_text(encoding="utf-8")
    require(driver_dialog, (
        "VID_16C0&PID_05DC",
        "VID_03EB&PID_C7B4",
        "SPDRP_SERVICE",
        "FlyingBytePro USBasp Driver Installation",
        "USBasp Driver Installation",
        ":/icons/FD-Logo.png",
        "Install Driver",
        "Microsoft WinUSB — %1",
        "wdi_create_list",
        "WDI_WINUSB",
        "wdi_prepare_driver",
        "wdi_install_driver",
        "listOptions.list_all = TRUE",
        "installOptions.hWnd = nullptr",
        "QThread::create",
        "Multiple matching USBasp devices are present",
        "packageId.replace(QLatin1Char(':')",
        "GetFileVersionInfoSizeW",
        "OpenProcessToken",
        "TokenElevation",
        "ShellExecuteExW",
        'shellInfo.lpVerb = L"runas"',
        "--flyingbytespro-winusb-helper",
    ), "V22-style direct USBasp WinUSB installer with isolated UAC helper")
    for retired_copy in ("no Zadig", "FlyingBytesPro stays unelevated", "Install / Repair Driver",
                         "Install or repair Microsoft WinUSB", "Install or repair the Windows WinUSB driver"):
        if retired_copy in driver_dialog or retired_copy in main_window:
            fail(f"retired verbose driver UI wording remains: {retired_copy}")
    for forbidden in ("WDI_LIBUSB0", "WDI_LIBUSBK", "libusb-win32", "libusbK.dll"):
        if forbidden in driver_dialog:
            fail(f"alternate USB driver support remains in WinUSB-only driver dialog: {forbidden}")
    for retired in ("QNetworkAccessManager", "QNetworkReply", "QDesktopServices",
                    "Official Download Page", "Advanced Driver Tool"):
        if retired in driver_dialog:
            fail(f"retired external driver-tool workflow remains: {retired}")

    main_cpp = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    require(main_cpp, (
        "runUsbAspDriverInstallHelper(QCoreApplication::arguments())",
        "if (driverHelperResult >= 0)",
    ), "hidden elevated same-executable WinUSB helper dispatch")
    if main_cpp.index("runUsbAspDriverInstallHelper") > main_cpp.index("QCommandLineParser parser"):
        fail("hidden WinUSB helper dispatch must occur before normal command-line parsing")

    prepare_libwdi = (ROOT / "scripts/Prepare-Libwdi.ps1").read_text(encoding="utf-8")
    require(prepare_libwdi, (
        "9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602",
        "resources\\libwdi",
        "include\\libwdi.h",
        "lib\\libwdi.a",
        "LICENSE.libwdi.txt",
        "VERSION.txt",
        "libwdi-source.zip",
        "Using bundled libwdi",
    ), "bundled WinUSB-only libwdi validation")
    for forbidden in ("Invoke-WebRequest", "msiexec.exe", "bootstrap.sh", "--with-wdkdir",
                      "libusb-win32", "libusbK", "third_party"):
        if forbidden in prepare_libwdi:
            fail(f"obsolete libwdi download/build dependency remains: {forbidden}")

    if (ROOT / "third_party").exists():
        fail("obsolete third_party directory remains after libwdi resource consolidation")
    if (ROOT / "samples").exists():
        fail("obsolete samples directory remains; use Hex_Sample")
    hex_samples = sorted((ROOT / "Hex_Sample").glob("*.hex"))
    if len(hex_samples) != 9:
        fail(f"Hex_Sample must contain the 9 supplied RandomData HEX files, found {len(hex_samples)}")

    display_language = (ROOT / "src/gui/DisplayLanguage.cpp").read_text(encoding="utf-8")
    require(display_language, (
        'QStringLiteral("en")', 'QStringLiteral("zh-CN")', 'QStringLiteral("es")',
        'QStringLiteral("ja")', 'QStringLiteral("de")', 'QStringLiteral("ko")',
        'QStringLiteral("fr")', 'QStringLiteral("vi")',
        'QStringLiteral(":/languages/%1.json")',
        "translateWidgetTree", "forwardTemplates", "reverseTemplates",
        "literalCharacters", "std::stable_sort(pack.forwardTemplates",
    ), "display-language presentation layer")
    require(main_window, (
        'QStringLiteral("displayLanguage")',
        'setProperty("displayLanguageSelector", true)',
        "DisplayLanguage::availableLanguages()",
        "applyDisplayLanguage(previousCode)",
        'QStringLiteral("Language: %1")',
        'settings.remove(QStringLiteral("displayLanguage"))',
        'settings.remove(QStringLiteral("confirmMemoryWrites"))',
        'settings.remove(QStringLiteral("verifyAfterWrite"))',
        'settings.remove(QStringLiteral("flashReadMode"))',
        'settings.remove(QStringLiteral("ignoreMcuSignatureMatching"))',
    ), "Settings language selector and Restore Defaults action")

    require(main_window, (
        "memoryBlock->setFixedWidth(ui.table->width())",
        "editRow->addStretch(1)",
        "openFuseAndLockDialog",
    ), "centered memory editor action row and unified fuse dialog")

    print("PASS: custom memory controls, themed Files controls, memory-only progress states, clock face, and persistent settings are present.")
    print(f"PASS: {len(fuse_ui.get('devices', []))} fuse/lock metadata entries loaded.")
    print(f"PASS: fuse/lock decoder lookup reaches {decoded_classic}/167 classic SPI-ISP device records.")
    print("PASS: queue-safe Flash writes, page-isolated EEPROM writes, and required-only long-address setup are present.")
    print("PASS: persistent optional MCU signature matching bypass is present.")
    print("PASS: direct USBasp command-line programmer and packaging checks are present.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
