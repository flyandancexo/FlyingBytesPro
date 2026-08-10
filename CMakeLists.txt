# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

project(FlyingBytesPro
  VERSION 3.2.29
  DESCRIPTION "Compact Qt USBasp AVR programmer and hexadecimal editor"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Widgets)
if(BUILD_TESTING)
  find_package(Qt6 6.11.1 REQUIRED COMPONENTS Test)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

qt_standard_project_setup()
qt_add_resources(APP_RESOURCES resources/resources.qrc)

add_library(usbasp_core STATIC
  src/usb/UsbAspProtocol.h
  src/usb/UsbAspDevice.h
  src/usb/UsbAspDevice.cpp
  src/avr/AvrDevice.h
  src/avr/AvrDevice.cpp
  src/avr/AvrDatabase.h
  src/avr/AvrDatabase.cpp
  src/avr/AvrIspProgrammer.h
  src/avr/AvrIspProgrammer.cpp
  src/firmware/FirmwareImage.h
  src/firmware/FirmwareImage.cpp
  src/firmware/IntelHex.h
  src/firmware/IntelHex.cpp
  src/firmware/Crc16.h
  src/firmware/Crc16.cpp
  src/firmware/ProjectFile.h
  src/firmware/ProjectFile.cpp
)

target_include_directories(usbasp_core PUBLIC src)
target_link_libraries(usbasp_core PUBLIC Qt6::Core PkgConfig::LIBUSB)
target_compile_definitions(usbasp_core PUBLIC QT_NO_KEYWORDS)
if(MINGW)
  target_compile_options(usbasp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(FLYINGBYTESPRO_GUI_SOURCES
  src/main.cpp
  src/gui/DisplayLanguage.h
  src/gui/DisplayLanguage.cpp
  src/gui/FuseLockDialog.h
  src/gui/FuseLockDialog.cpp
  src/gui/HexTableModel.h
  src/gui/HexTableModel.cpp
  src/gui/ProgrammerController.h
  src/gui/ProgrammerController.cpp
  src/gui/SckDial.h
  src/gui/SckDial.cpp
  src/gui/UsbAspDriverDialog.h
  src/gui/UsbAspDriverDialog.cpp
  src/gui/MemoryMeterButton.h
  src/gui/MemoryMeterButton.cpp
  src/gui/MainWindow.h
  src/gui/MainWindow.cpp
  ${APP_RESOURCES}
)
if(WIN32)
  set(FLYINGBYTESPRO_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/app.ico")
  configure_file(
    resources/windows/app.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/FlyingBytesPro.rc"
    @ONLY
  )
  list(APPEND FLYINGBYTESPRO_GUI_SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/FlyingBytesPro.rc"
  )
endif()

qt_add_executable(FlyingBytesPro WIN32 ${FLYINGBYTESPRO_GUI_SOURCES})
target_link_libraries(FlyingBytesPro PRIVATE usbasp_core Qt6::Gui Qt6::Widgets)
if(WIN32)
  set(FLYINGBYTESPRO_LIBWDI_ROOT
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/libwdi"
      CACHE PATH "Bundled libwdi 1.5.1 root used by the direct USBasp WinUSB installer")
  find_path(LIBWDI_INCLUDE_DIR
    NAMES libwdi.h
    PATHS "${FLYINGBYTESPRO_LIBWDI_ROOT}/include"
    NO_DEFAULT_PATH
  )
  find_library(LIBWDI_LIBRARY
    NAMES wdi libwdi
    PATHS "${FLYINGBYTESPRO_LIBWDI_ROOT}/lib"
    NO_DEFAULT_PATH
  )
  if(NOT LIBWDI_INCLUDE_DIR OR NOT LIBWDI_LIBRARY)
    message(FATAL_ERROR
      "Bundled libwdi 1.5.1 was not found. Expected resources/libwdi")
  endif()
  target_include_directories(FlyingBytesPro PRIVATE "${LIBWDI_INCLUDE_DIR}")
  target_link_libraries(FlyingBytesPro PRIVATE
    "${LIBWDI_LIBRARY}"
    setupapi newdev advapi32 crypt32 wintrust ole32 ntdll user32 shell32 version
  )
endif()
target_compile_definitions(FlyingBytesPro PRIVATE QT_NO_KEYWORDS)

add_executable(usbasp_probe src/cli/usbasp_probe.cpp)
target_link_libraries(usbasp_probe PRIVATE usbasp_core Qt6::Core)
target_compile_definitions(usbasp_probe PRIVATE QT_NO_KEYWORDS)

add_executable(FlyingBytesProCLI
  src/cli/flyingbytespro_cli.cpp
  ${APP_RESOURCES}
)
target_link_libraries(FlyingBytesProCLI PRIVATE usbasp_core Qt6::Core)
target_compile_definitions(FlyingBytesProCLI PRIVATE QT_NO_KEYWORDS)

if(BUILD_TESTING)
  qt_add_executable(test_firmware tests/test_firmware.cpp)
  target_link_libraries(test_firmware PRIVATE usbasp_core Qt6::Test)
  target_compile_definitions(test_firmware PRIVATE QT_NO_KEYWORDS)
  add_test(NAME firmware_tests COMMAND test_firmware)
  add_test(NAME cli_help COMMAND FlyingBytesProCLI --help)
  add_test(NAME cli_list_parts COMMAND FlyingBytesProCLI --list-parts)
endif()

install(TARGETS FlyingBytesPro FlyingBytesProCLI usbasp_probe RUNTIME DESTINATION .)
