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
    )
    for item in required:
        if not (ROOT / item).is_file():
            fail(f"required file missing: {item}")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if "project(FlyingBytesPro" not in cmake or "VERSION 3.2.20" not in cmake:
        fail("CMake project name/version is incorrect")
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
    placeholder = re.compile(r"%[1-9]")
    for code in sorted(expected_languages):
        pack_path = ROOT / f"resources/languages/{code}.json"
        pack = json.loads(pack_path.read_text(encoding="utf-8"))
        strings = pack.get("strings", {})
        if pack.get("language") != code or len(strings) < 220:
            fail(f"language pack is incomplete: {code}")
        for source, target in strings.items():
            if not source or not isinstance(target, str) or not target:
                fail(f"language pack contains an empty entry: {code}: {source!r}")
            if sorted(placeholder.findall(source)) != sorted(placeholder.findall(target)):
                fail(f"language placeholder mismatch: {code}: {source}")

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
        "MCU_Search_B.png", "MCU_Search.png", "Mem-Flash_B.png", "Mem-EEPROM_B.png",
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
        "Mem-EEPROM_B.png": "935e3608fe4fc04b9e4aa8bd1d6bef962ef1b6849d84c9ca024f7c6d545f32a2",
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
        "# FlyingBytesPro V3.2.20",
        "## Highlights",
        "**Supports 175 AVR microcontrollers: 167 classic SPI-ISP devices plus 8 TPI devices.**",
        "Completely rewritten backend for direct USBasp programming through libusb",
        "Smart and Full MCU Flash-read modes",
        "Editable hexadecimal and ASCII memory buffers with compact centered memory tables, fixed HEX Address/byte/DEC Address/ASCII columns, center-aligned ASCII display, no unused gap before the vertical scrollbar, Clear all/Clear Selected, Zero all/Zero Selected",
        "TECHNICAL_README.md",
        "Buy Me a Coffee",
        "GPL-3.0-or-later",
    ), "showcase README")
    technical = (ROOT / "TECHNICAL_README.md").read_text(encoding="utf-8")
    require(technical, (
        "# FlyingBytesPro V3.2.20 — Technical Reference and Development History",
        "The next development version is **V3.2.21**.",
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
        "FlyingBytesPro V3.2.20",
    ), "project schema-2 clock migration")

    database = json.loads((ROOT / "resources/devices/avr_devices.json").read_text(encoding="utf-8"))
    devices = database.get("devices", [])
    if len(devices) != 175:
        fail("embedded candidate must contain 175 device records")
    tpi_devices = [device for device in devices if device.get("programmingInterface") == "tpi"]
    if len(tpi_devices) != 8:
        fail("embedded candidate must contain exactly 8 TPI device records")

    main_cpp = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    if "FlyingBytesPro" not in main_cpp or "V3.2.20" not in main_cpp:
        fail("application identity is missing from src/main.cpp")

    main_window = (ROOT / "src/gui/MainWindow.cpp").read_text(encoding="utf-8")
    bad_programmer_spelling = "Program" + "mor"
    if bad_programmer_spelling in main_window:
        fail("misspelled programmer status remains in MainWindow.cpp")
    require(main_window, (
        "FlyingBytesPro V3.2.20", "Task", "Flash", "EEPROM", "Full Log",
        "Settings", "About", "Log", "Save Project", "Load Project",
        ":/icons/MCU_Search_B.png", ":/icons/Mem-Flash_B.png",
        ":/icons/Mem-EEPROM_B.png", ":/icons/Button_Start.png", ":/icons/FD-Logo.png",
        "m_startTaskButton->setFixedSize(170, 46)",
        "m_startTaskButton->setIconSize(QSize(164, 39))",
        ":/icons/Pigeon_Coffee.png", "Buy Me a Coffee",
        "Supports 175 AVR microcontrollers: 167 through SPI ISP plus 8 through TPI.",
        "Sophisticated and beautiful GUI with a simple, focused design.",
        "New AVRDUDE-style command-line interface using the USBasp protocol.",
        "Auto SCK scans from the fastest supported clock downward; Pro uses the USBasp firmware default clock request",
        "Smart and Full MCU Flash reads with automatic erased-tail cleanup.",
        "Open-source software licensed under GNU GPL-3.0-or-later.",
        "by Flyandance JZ from San Francisco, 2026",
        "Program Fuses",
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
        'setMinimumSize(1140, 650);',
        'resize(1140, 780);',
        'windowLayoutSchema < 3',
        'QStringLiteral("mainWindowLayoutSchema"), 3',
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
    ), "requested V3.2.20 UI behavior")
    require(main_window, (
        '"<b>ID:</b> %1<br>"',
        'QStringLiteral("<b>ID:</b> -- -- --<br>"',
        'layout->addWidget(languageBox);',
        'QStringLiteral("Language: %1")',
    ), "V3.2.20 MCU ID, Settings language position, and compact language status")
    if 'Display language changed to %1. Programming logic' in main_window:
        fail("verbose language-change runtime message remains")
    if 'setStretchLastSection(true)' in main_window or 'QSizePolicy::Expanding' in main_window:
        fail("memory tabs still contain the reverted V3.2.17 full-width stretch behavior")
    if 'setMinimumWidth(930)' in main_window or 'setMaximumWidth(1040)' in main_window:
        fail("memory table still uses the oversized V3.2.18 width range that leaves a gap before the scrollbar")
    signature_pos = main_window.find('layout->addWidget(signatureBox);')
    language_pos = main_window.find('layout->addWidget(languageBox);', signature_pos)
    stretch_pos = main_window.find('layout->addStretch();', language_pos)
    if signature_pos < 0 or language_pos < 0 or stretch_pos < 0 or not (signature_pos < language_pos < stretch_pos):
        fail("Language must be the last Settings list item before the trailing stretch")

    hex_table = (ROOT / "src/gui/HexTableModel.cpp").read_text(encoding="utf-8")
    require(hex_table, (
        "if (index.column() == kAsciiColumn)",
        "if (section == kAsciiColumn)",
        "return static_cast<int>(Qt::AlignCenter);",
    ), "center-aligned ASCII memory column")

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
        'MCU signature %1 matches selected %2.',
        'MCU signature %1 does not match selected %2 [%3].',
        'm_tabs->addTab(m_flashUi.page, mcuClassIcon(QStringLiteral("ATmega"))',
        'm_tabs->addTab(m_eepromUi.page, mcuClassIcon(QStringLiteral("ATtiny"))',
        "<b>ID:</b> %1<br>",
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
        'QStringLiteral("OKAY! ☆☆☆")',
        'QStringLiteral("ERROR ★★★")',
        'QStringLiteral("Programmer Online :)")',
        'QStringLiteral("Programmer Offline :(")',
        'completeProgressResult(success)',
    ), "readable result markers, USBasp presence wording, checksum text, and memory-only progress colors")
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
        'm_taskChecks[1]->isChecked() || m_taskChecks[3]->isChecked()',
        'm_taskQueue.append(TaskStep::EraseChip);',
        'm_taskElapsedTimer.start();',
        'm_taskElapsedTimer.elapsed()',
        'summary = QStringLiteral("%1 completed in %2.")',
        'taskSummaryItem(m_activeTask, message)',
        "m_taskSummaryItems.join(QLatin1Char(\' \'))",
        'if (m_miniLog) m_miniLog->clear();',
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
        "imageForWrite", "The empty %1 buffer is being programmed as 0x00",
    ), "shared-signature, remembered-folder, and empty-buffer behavior")
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
    ), "signature-bypass enforcement and post-write ISP-session recovery")
    if programmer.count("m_refreshSessionBeforeNextDeviceAccess = totalBytes > 0;") != 2:
        fail("Flash and EEPROM writes must both request a clean ISP session before the next device access")

    controller = (ROOT / "src/gui/ProgrammerController.cpp").read_text(encoding="utf-8")
    if bad_programmer_spelling in controller:
        fail("misspelled programmer status remains in ProgrammerController.cpp")
    require(controller, (
        "ignoreMcuSignatureMatchingSetting",
        'QStringLiteral("ignoreMcuSignatureMatching")',
        "AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());",
        "transferRateText", "elapsedTimeText",
        "%1 bytes read from Flash at %2 in %3",
        "%1 bytes read from EEPROM at %2 in %3",
        "%1 bytes written to Flash at %2 in %3%4",
        "%1 bytes written to EEPROM at %2 in %3%4",
        "programmer.lastTransferBytes()",
        "programmer.lastTransferElapsedMs()",
        "QElapsedTimer operationTimer",
        'QStringLiteral(" [Verified; total %1]")',
        "m_progressEmitTimer.elapsed() >= 40",
        'QStringLiteral("Programmer Online :)")',
        'QStringLiteral("%1s")',
        "KB/s", "B/s",
    ), "optimized transfer-rate and progress reporting")

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
    ), "byte editor with dual addresses and undoable Clear/Zero operations")
    fill_block = model[model.index("void HexTableModel::fill("):
                       model.index("void HexTableModel::clearBuffer()")]
    if "m_undo.clear()" in fill_block or "m_undo.setClean()" in fill_block:
        fail("Fill with FF must preserve the existing undo/redo history")

    cli = (ROOT / "src/cli/flyingbytespro_cli.cpp").read_text(encoding="utf-8")
    require(cli, (
        "FlyingBytesProCLI V3.2.20",
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
    if "AVRDUDE" in cli and "does not launch AVRDUDE" not in (ROOT / "CLI_GUIDE.md").read_text(encoding="utf-8"):
        fail("CLI documentation must state that AVRDUDE is not launched")
    if "add_executable(FlyingBytesProCLI" not in cmake:
        fail("CMake does not build FlyingBytesProCLI")
    if "FlyingBytesProCLI.exe" not in (ROOT / "scripts/Build-And-Run.ps1").read_text(encoding="utf-8"):
        fail("build script does not package FlyingBytesProCLI.exe")

    build_script = (ROOT / "scripts/Build-And-Run.ps1").read_text(encoding="utf-8")
    if "explorer.exe" in build_script:
        fail("build script must not open the portable output folder")

    app_rc = (ROOT / "resources/windows/app.rc.in").read_text(encoding="utf-8")
    if "FILEVERSION 3,2,20,0" not in app_rc or '"V3.2.20\\0"' not in app_rc:
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
    ), "Settings language selector")

    require(main_window, (
        "memoryBlock->setFixedWidth(ui.table->width())",
        "editRow->addStretch()",
        "openFuseAndLockDialog",
    ), "centered memory editor action row and unified fuse dialog")

    print("PASS: custom memory controls, themed Files controls, memory-only progress states, clock face, and persistent settings are present.")
    print(f"PASS: {len(fuse_ui.get('devices', []))} fuse/lock metadata entries loaded.")
    print("PASS: queue-safe Flash writes, page-isolated EEPROM writes, and required-only long-address setup are present.")
    print("PASS: persistent optional MCU signature matching bypass is present.")
    print("PASS: direct USBasp command-line programmer and packaging checks are present.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
